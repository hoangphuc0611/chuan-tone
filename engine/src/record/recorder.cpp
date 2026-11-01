#include "record/recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace tonecore {

namespace {

constexpr double kDefaultMaxSeconds = 15.0 * 60.0;
constexpr double kLimitMaxSeconds = 60.0 * 60.0;

}  // namespace

Recorder::Recorder(uint32_t sampleRate, uint32_t maxBlockFrames, double ringSeconds)
    : sampleRate_(sampleRate),
      maxBlockFrames_(maxBlockFrames),
      ringFloats_(static_cast<std::size_t>(std::max(ringSeconds, 0.01) * sampleRate) * kChannels),
      interleave_(static_cast<std::size_t>(maxBlockFrames) * kChannels) {}

Recorder::~Recorder() {
  // Engine đã dừng audio trước khi huỷ các thành viên → chốt ngay, không chờ callback.
  if (busy()) stop(true);
  joinWriter();
}

bool Recorder::busy() const noexcept {
  const int s = state_.load(std::memory_order_acquire);
  return s != kIdle && s != kDone && s != kFailed;
}

void Recorder::joinWriter() {
  if (writer_.joinable()) writer_.join();
}

tc_result Recorder::start(const std::string& dir, double maxSeconds, const RecordStartInfo& info) {
  if (busy()) return TC_ERR_INVALID_STATE;
  if (dir.empty() || !(maxSeconds >= 0.0)) return TC_ERR_INVALID_ARG;
  joinWriter();  // luồng ghi của lần trước đã kết thúc (Done/Failed) — chỉ dọn

  const std::string base = dir.back() == '/' ? dir : dir + "/";
  tc_result r = dry_.open(base + "dry.wav", 1, sampleRate_, WavFormat::Float32);
  if (r == TC_OK) r = wet_.open(base + "wet.wav", 2, sampleRate_, WavFormat::Int16);
  if (r == TC_OK) r = beat_.open(base + "beat.wav", 2, sampleRate_, WavFormat::Int16);
  if (r != TC_OK) {
    dry_.abort();
    wet_.abort();
    beat_.abort();
    return r;
  }
  if (!ring_) ring_ = std::make_unique<SpscRing<float>>(ringFloats_);
  // Bỏ phần sót lại (lần trước bị chốt khi audio thread không xác nhận kịp). Luồng ghi đã dừng
  // nên luồng này tạm làm bên đọc.
  float sink[256];
  while (ring_->pop(sink, 256) > 0) {
  }

  const double secs =
      maxSeconds == 0.0 ? kDefaultMaxSeconds : std::min(maxSeconds, kLimitMaxSeconds);
  maxFrames_ = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(secs * sampleRate_)));
  info_ = info;
  result_.store(TC_OK, std::memory_order_relaxed);
  framesCaptured_.store(0, std::memory_order_relaxed);
  framesWritten_.store(0, std::memory_order_relaxed);
  dropped_.store(0, std::memory_order_relaxed);
  startBeatFrame_.store(0, std::memory_order_relaxed);
  startHostTimeNs_.store(0, std::memory_order_relaxed);
  state_.store(kArmed, std::memory_order_release);
  writer_ = std::thread([this] { writerLoop(); });
  return TC_OK;
}

tc_result Recorder::stop(bool audioStopped) {
  int s = state_.load(std::memory_order_acquire);
  while (s == kArmed || s == kRecording) {
    if (state_.compare_exchange_weak(s, kStopRequested, std::memory_order_acq_rel)) {
      s = kStopRequested;
      break;
    }
  }
  if (s == kStopRequested) {
    // Audio thread xác nhận ở block kế tiếp (vài ms). Không còn callback (engine dừng, bị ngắt
    // bởi cuộc gọi…) → tự chốt: audio thread chỉ đẩy vào ring khi thấy Recording, và luồng ghi
    // vẫn rút ring tới khi thấy Draining nên không mất gì đã đẩy.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!audioStopped && state_.load(std::memory_order_acquire) == kStopRequested &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    int expected = kStopRequested;
    state_.compare_exchange_strong(expected, kDraining, std::memory_order_acq_rel);
  }
  joinWriter();
  s = state_.load(std::memory_order_acquire);
  if (s == kIdle) return TC_ERR_INVALID_STATE;
  return result_.load(std::memory_order_relaxed);
}

// [RT]
void Recorder::capture(const float* dry, const float* wetL, const float* wetR, const float* beat,
                       uint32_t frames, uint64_t beatFrame, int64_t hostTimeNs) noexcept {
  int s = state_.load(std::memory_order_acquire);
  if (s == kStopRequested) {
    state_.compare_exchange_strong(s, kDraining, std::memory_order_acq_rel);
    return;
  }
  if (s == kArmed) {
    startBeatFrame_.store(beatFrame, std::memory_order_relaxed);
    startHostTimeNs_.store(hostTimeNs, std::memory_order_relaxed);
    if (!state_.compare_exchange_strong(s, kRecording, std::memory_order_acq_rel)) {
      // stop() vừa chen vào: xác nhận luôn.
      if (s == kStopRequested) state_.compare_exchange_strong(s, kDraining);
      return;
    }
  } else if (s != kRecording) {
    return;
  }
  frames = std::min(frames, maxBlockFrames_);
  const uint64_t done = framesCaptured_.load(std::memory_order_relaxed);
  const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(frames, maxFrames_ - done));
  float* x = interleave_.data();
  for (uint32_t i = 0; i < n; ++i) {
    x[0] = dry[i];
    x[1] = wetL[i];
    x[2] = wetR[i];
    x[3] = beat[2 * i];
    x[4] = beat[2 * i + 1];
    x += kChannels;
  }
  const std::size_t count = static_cast<std::size_t>(n) * kChannels;
  // sizeApprox từ phía ghi chỉ có thể lớn hơn thật (luồng đọc chỉ làm nó nhỏ đi) → chỗ trống tính
  // ra là chắc chắn có.
  if (ring_->capacity() - ring_->sizeApprox() >= count) {
    ring_->push(interleave_.data(), count);
  } else {
    dropped_.fetch_add(n, std::memory_order_relaxed);
  }
  framesCaptured_.store(done + n, std::memory_order_relaxed);
  if (done + n >= maxFrames_) {
    s = kRecording;
    state_.compare_exchange_strong(s, kDraining, std::memory_order_acq_rel);
  }
}

void Recorder::writerLoop() {
  constexpr std::size_t kChunkFrames = 4096;
  std::vector<float> buf(kChunkFrames * kChannels);
  std::vector<float> dry(kChunkFrames);
  std::vector<float> wet(kChunkFrames * 2);
  std::vector<float> beat(kChunkFrames * 2);
  bool ok = true;
  for (;;) {
    // Đọc trạng thái TRƯỚC khi rút: thấy Draining rồi mà ring rỗng thì chắc chắn audio thread
    // không đẩy thêm nữa.
    const int s = state_.load(std::memory_order_acquire);
    const bool draining = s == kDraining;
    std::size_t got = 0;
    if (!writerPaused_.load(std::memory_order_relaxed)) {
      while ((got = ring_->pop(buf.data(), buf.size())) > 0) {
        const std::size_t frames = got / kChannels;  // luôn đẩy / rút nguyên frame
        for (std::size_t i = 0; i < frames; ++i) {
          const float* x = buf.data() + i * kChannels;
          dry[i] = x[0];
          wet[2 * i] = x[1];
          wet[2 * i + 1] = x[2];
          beat[2 * i] = x[3];
          beat[2 * i + 1] = x[4];
        }
        if (ok) {
          ok = dry_.write(dry.data(), frames) && wet_.write(wet.data(), frames) &&
               beat_.write(beat.data(), frames);
          if (ok) framesWritten_.fetch_add(frames, std::memory_order_relaxed);
        }
      }
    }
    if (draining && !writerPaused_.load(std::memory_order_relaxed)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Ba file cùng chốt hoặc cùng huỷ.
  if (ok) ok = dry_.finish() && wet_.finish() && beat_.finish();
  if (!ok) {
    dry_.abort();
    wet_.abort();
    beat_.abort();
    result_.store(TC_ERR_INTERNAL, std::memory_order_relaxed);
  }
  state_.store(ok ? kDone : kFailed, std::memory_order_release);
}

void Recorder::fillStatus(tc_record_status& s) const noexcept {
  const int st = state_.load(std::memory_order_acquire);
  switch (st) {
    case kArmed:
    case kRecording:
      s.state = TC_RECORD_RECORDING;
      break;
    case kStopRequested:
    case kDraining:
      s.state = TC_RECORD_FINISHING;
      break;
    case kDone:
      s.state = TC_RECORD_DONE;
      break;
    case kFailed:
      s.state = TC_RECORD_FAILED;
      break;
    default:
      s.state = TC_RECORD_IDLE;
      break;
  }
  s.result = result_.load(std::memory_order_relaxed);
  s.sample_rate = sampleRate_;
  s.frames = st == kDone ? framesWritten_.load(std::memory_order_relaxed)
                         : framesCaptured_.load(std::memory_order_relaxed);
  s.dropped_frames = dropped_.load(std::memory_order_relaxed);
  const bool started = st != kIdle && st != kArmed;
  s.start_beat_frame = started ? startBeatFrame_.load(std::memory_order_relaxed) : 0;
  s.start_host_time_ns = started ? startHostTimeNs_.load(std::memory_order_relaxed) : 0;
  s.input_latency_ms = info_.inputLatencyMs;
  s.output_latency_ms = info_.outputLatencyMs;
  s.pitch_latency_frames = info_.pitchLatencyFrames;
  s.output_lookahead_frames = info_.outputLookaheadFrames;
  s.route = info_.route;
}

}  // namespace tonecore
