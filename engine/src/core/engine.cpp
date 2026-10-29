#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "core/denormal.h"
#include "decode/decoder.h"

namespace tonecore {

namespace {
constexpr double kGainSmoothingSec = 0.02;
constexpr double kMonitorFadeSec = 0.01;
// Limiter đầu ra (docs/m5-plan.md điểm 3): giọng nghe lại trễ thêm đúng 1 ms.
constexpr float kLimiterCeiling = 0.98f;
constexpr double kLimiterLookaheadMs = 1.0;
constexpr double kLimiterReleaseMs = 60.0;
}  // namespace

Engine::Engine(const EngineConfig& config)
    : config_(config),
      player_(config.sampleRate),
      pitch_(config.sampleRate, config.maxBlockFrames),
      probe_(config.sampleRate),
      limiter_(2, config.sampleRate, kLimiterCeiling, kLimiterLookaheadMs, kLimiterReleaseMs),
      fxRack_(config.sampleRate, config.maxBlockFrames),
      micBuf_(config.maxBlockFrames, 0.0f),
      beatBuf_(2 * static_cast<std::size_t>(config.maxBlockFrames), 0.0f),
      stimBuf_(config.maxBlockFrames, 0.0f),
      voiceL_(config.maxBlockFrames, 0.0f),
      voiceR_(config.maxBlockFrames, 0.0f),
      mixBuf_(2 * static_cast<std::size_t>(config.maxBlockFrames), 0.0f),
      outBuf_(2 * static_cast<std::size_t>(config.maxBlockFrames), 0.0f) {
  masterGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  monitorGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  beatGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  monitorMix_.prepare(config_.sampleRate, kMonitorFadeSec);
  latencyResult_.struct_size = sizeof(tc_latency_result);
}

Engine::~Engine() {
  if (state() == TC_STATE_RUNNING) stop();
  // Không còn audio thread: giải phóng mọi timeline còn giữ.
  KeyTimeline* t = nullptr;
  while (timelineIncoming_.pop(t)) delete t;
  collectTimelineGarbage();
  delete timeline_;
}

// ---------------------------------------------------------------------------
// Vòng đời

tc_result Engine::start() {
  if (state() != TC_STATE_STOPPED) return TC_ERR_INVALID_STATE;

  if (config_.backend == TC_BACKEND_PLATFORM) {
    backend_ = createPlatformBackend(*this, config_);
    if (!backend_) return TC_ERR_NOT_SUPPORTED;
    const tc_result r = backend_->start();
    if (r != TC_OK) {
      backend_.reset();
      return r;
    }
    const BackendInfo info = backend_->info();
    if (info.route != TC_ROUTE_UNKNOWN) route_.store(info.route, std::memory_order_relaxed);
    updateMonitorTarget();
  }
  state_.store(TC_STATE_RUNNING, std::memory_order_release);
  return TC_OK;
}

tc_result Engine::stop() {
  if (state() != TC_STATE_RUNNING) return TC_ERR_INVALID_STATE;
  state_.store(TC_STATE_STOPPED, std::memory_order_release);
  if (backend_) {
    backend_->stop();  // sau lời gọi này không còn callback nào
    backend_.reset();
  }
  probe_.cancel();
  return TC_OK;
}

// ---------------------------------------------------------------------------
// Tham số

void Engine::updateMonitorTarget() noexcept {
  const bool blocked = route_.load(std::memory_order_relaxed) == TC_ROUTE_BLUETOOTH;
  const bool on = monitorEnabled_.load(std::memory_order_relaxed) && !blocked;
  monitorMix_.setTarget(on ? 1.0f : 0.0f);
}

tc_result Engine::setParam(int32_t nodeId, int32_t paramId, float value) noexcept {
  switch (nodeId) {
    case TC_NODE_MASTER:
      if (paramId == TC_PARAM_GAIN) {
        masterGain_.setTarget(value);
        return TC_OK;
      }
      break;
    case TC_NODE_MONITOR:
      if (paramId == TC_PARAM_GAIN) {
        monitorGain_.setTarget(value);
        return TC_OK;
      }
      if (paramId == TC_PARAM_ENABLED) {
        const bool on = value >= 0.5f;
        monitorEnabled_.store(on, std::memory_order_relaxed);
        updateMonitorTarget();
        if (on && route_.load(std::memory_order_relaxed) == TC_ROUTE_BLUETOOTH) {
          events_.push(TC_EVENT_MONITOR_BLOCKED);
        }
        return TC_OK;
      }
      break;
    case TC_NODE_BEAT:
      if (paramId == TC_PARAM_GAIN) {
        beatGain_.setTarget(value);
        return TC_OK;
      }
      break;
    case TC_NODE_GATE:
      if (!std::isfinite(value)) return TC_ERR_INVALID_ARG;
      if (paramId == TC_PARAM_ENABLED) {
        pitch_.setGateEnabled(value >= 0.5f);
        return TC_OK;
      }
      if (paramId == TC_PARAM_THRESHOLD_DB) {
        pitch_.setGateThresholdDb(value);
        return TC_OK;
      }
      break;
    case TC_NODE_FX:
      if (!std::isfinite(value)) return TC_ERR_INVALID_ARG;
      if (paramId == TC_PARAM_ENABLED) {
        fxRack_.setEnabled(value >= 0.5f);
        return TC_OK;
      }
      if (paramId == TC_PARAM_MIX) {
        fxRack_.setWet(value);
        return TC_OK;
      }
      break;
    case TC_NODE_PITCH:
      if (!std::isfinite(value)) return TC_ERR_INVALID_ARG;
      switch (paramId) {
        case TC_PARAM_ENABLED:
          pitch_.setEnabled(value >= 0.5f);
          return TC_OK;
        case TC_PARAM_RETUNE_MS:
          pitch_.setRetuneMs(value);
          return TC_OK;
        case TC_PARAM_HUMANIZE:
          pitch_.setHumanize(value);
          return TC_OK;
        case TC_PARAM_FLEX_CENTS:
          pitch_.setFlexCents(value);
          return TC_OK;
        case TC_PARAM_VIBRATO_PRESERVE:
          pitch_.setVibratoPreserve(value);
          return TC_OK;
        case TC_PARAM_HYSTERESIS_CENTS:
          pitch_.setHysteresisCents(value);
          return TC_OK;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return TC_ERR_INVALID_ARG;
}

tc_result Engine::getParam(int32_t nodeId, int32_t paramId, float* outValue) const noexcept {
  if (paramId == TC_PARAM_GAIN) {
    switch (nodeId) {
      case TC_NODE_MASTER:
        *outValue = masterGain_.target();
        return TC_OK;
      case TC_NODE_MONITOR:
        *outValue = monitorGain_.target();
        return TC_OK;
      case TC_NODE_BEAT:
        *outValue = beatGain_.target();
        return TC_OK;
      default:
        return TC_ERR_INVALID_ARG;
    }
  }
  if (paramId == TC_PARAM_ENABLED && nodeId == TC_NODE_MONITOR) {
    *outValue = monitorEnabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    return TC_OK;
  }
  if (nodeId == TC_NODE_GATE) {
    if (paramId == TC_PARAM_ENABLED) {
      *outValue = pitch_.gateEnabled() ? 1.0f : 0.0f;
      return TC_OK;
    }
    if (paramId == TC_PARAM_THRESHOLD_DB) {
      *outValue = pitch_.gateThresholdDb();
      return TC_OK;
    }
    return TC_ERR_INVALID_ARG;
  }
  if (nodeId == TC_NODE_FX) {
    if (paramId == TC_PARAM_ENABLED) {
      *outValue = fxRack_.enabled() ? 1.0f : 0.0f;
      return TC_OK;
    }
    if (paramId == TC_PARAM_MIX) {
      *outValue = fxRack_.wet();
      return TC_OK;
    }
    return TC_ERR_INVALID_ARG;
  }
  if (nodeId == TC_NODE_PITCH) {
    const CorrectorParams p = pitch_.params();
    switch (paramId) {
      case TC_PARAM_ENABLED:
        *outValue = pitch_.enabled() ? 1.0f : 0.0f;
        return TC_OK;
      case TC_PARAM_RETUNE_MS:
        *outValue = p.retuneMs;
        return TC_OK;
      case TC_PARAM_HUMANIZE:
        *outValue = p.humanize;
        return TC_OK;
      case TC_PARAM_FLEX_CENTS:
        *outValue = p.flexCents;
        return TC_OK;
      case TC_PARAM_VIBRATO_PRESERVE:
        *outValue = p.vibratoPreserve;
        return TC_OK;
      case TC_PARAM_HYSTERESIS_CENTS:
        *outValue = p.hysteresisCents;
        return TC_OK;
      default:
        break;
    }
  }
  return TC_ERR_INVALID_ARG;
}

// ---------------------------------------------------------------------------
// Tự chỉnh giọng

tc_result Engine::setKey(int32_t root, int32_t scale) noexcept {
  if (root < 0 || root > 11 || scale < 0 || scale >= kScaleCount) return TC_ERR_INVALID_ARG;
  followTimeline_.store(false, std::memory_order_release);  // người dùng chọn tay
  pitch_.setKey(root, static_cast<Scale>(scale));
  return TC_OK;
}

void Engine::appliedKey(int32_t& root, int32_t& scale) const noexcept {
  key(root, scale);
  root = (root + player_.audibleSemitones() + 12) % 12;
}

void Engine::key(int32_t& root, int32_t& scale) const noexcept {
  if (followTimeline_.load(std::memory_order_acquire)) {
    const int32_t k = autoKey_.load(std::memory_order_relaxed);
    root = k & 0xFF;
    scale = k >> 8;
    return;
  }
  root = pitch_.keyRoot();
  scale = static_cast<int32_t>(pitch_.keyScale());
}

tc_result Engine::setTuning(float cents) noexcept {
  if (!std::isfinite(cents) || std::fabs(cents) > 100.0f) return TC_ERR_INVALID_ARG;
  pitch_.setTuningCents(cents);
  return TC_OK;
}

void Engine::collectTimelineGarbage() {
  KeyTimeline* t = nullptr;
  while (timelineRetired_.pop(t)) delete t;
}

tc_result Engine::setKeyTimeline(const tc_key_segment* segments, uint32_t count) {
  constexpr uint32_t kMaxSegments = 256;
  if (count > kMaxSegments || (segments == nullptr && count > 0)) return TC_ERR_INVALID_ARG;
  collectTimelineGarbage();
  if (count == 0) {
    followTimeline_.store(false, std::memory_order_release);
    return TC_OK;
  }
  // Kiểm tra: đoạn hợp lệ, tăng dần, không chồng nhau.
  for (uint32_t i = 0; i < count; ++i) {
    const tc_key_segment& g = segments[i];
    if (!std::isfinite(g.start_ms) || !std::isfinite(g.end_ms) || g.start_ms < 0.0 ||
        g.end_ms <= g.start_ms || g.root < 0 || g.root > 11 || g.scale < 0 ||
        g.scale >= kScaleCount || (i > 0 && g.start_ms < segments[i - 1].end_ms - 1e-6)) {
      return TC_ERR_INVALID_ARG;
    }
  }
  auto tl = std::make_unique<KeyTimeline>();
  tl->entries.reserve(count);
  const double framesPerMs = config_.sampleRate / 1000.0;
  for (uint32_t i = 0; i < count; ++i) {
    tl->entries.push_back({static_cast<uint64_t>(std::llround(segments[i].start_ms * framesPerMs)),
                           segments[i].root, static_cast<Scale>(segments[i].scale)});
  }
  // Key của đoạn tại vị trí hiện tại → tc_get_key đúng ngay, trước cả khi audio thread chạy.
  const uint64_t pos = player_.positionFrames();
  const KeyTimeline::Entry* cur = &tl->entries.front();
  for (const auto& e : tl->entries) {
    if (e.startFrame <= pos) cur = &e;
  }
  const int32_t initial = packKey(cur->root, cur->scale);
  KeyTimeline* raw = tl.release();
  if (!timelineIncoming_.push(raw)) {
    delete raw;
    return TC_ERR_INVALID_STATE;  // audio thread chưa nhận các timeline trước (engine dừng?)
  }
  autoKey_.store(initial, std::memory_order_relaxed);
  followTimeline_.store(true, std::memory_order_release);
  return TC_OK;
}

tc_result Engine::setCorrectionPreset(int32_t preset) noexcept {
  if (preset < TC_PRESET_NATURAL || preset > TC_PRESET_ROBOT) return TC_ERR_INVALID_ARG;
  pitch_.applyPreset(static_cast<CorrectionPreset>(preset));
  return TC_OK;
}

void Engine::fillPitchState(tc_pitch_state& s) const noexcept {
  PitchSnapshot snap;
  pitch_.readState(snap);
  s.voiced = snap.voiced ? 1 : 0;
  s.f0_hz = snap.f0;
  s.confidence = snap.confidence;
  s.input_note = snap.inputMidi;
  s.target_note = snap.targetNote;
  s.cents_off = snap.centsOff;
  s.correction_cents = snap.correctionCents;
  s.enabled = pitch_.enabled() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Beat

tc_result Engine::loadBeat(const std::string& path) {
  if (path.empty()) return TC_ERR_INVALID_ARG;
  const uint32_t rate = config_.sampleRate;
  worker_.post([this, path, rate] {
    DecodedAudio decoded;
    tc_result r = decodeFile(path, decoded);
    double durationMs = 0.0;
    if (r == TC_OK) {
      std::shared_ptr<const Track> track = prepareTrack(std::move(decoded), rate);
      if (!track) {
        r = TC_ERR_NOT_SUPPORTED;
      } else {
        durationMs = 1000.0 * static_cast<double>(track->frames) / rate;
        // Beat mới → tone về 0 (app tự đặt lại tone đã nhớ cho bài).
        transposeGen_.fetch_add(1, std::memory_order_relaxed);
        if (requestedTrack_) requestedTrack_->cancelled.store(true, std::memory_order_relaxed);
        requestedTrack_.reset();
        requestedSemis_.store(0, std::memory_order_relaxed);
        transposeProgress_.store(1.0f, std::memory_order_relaxed);
        if (player_.setTrack(track)) {
          currentTrack_ = std::move(track);
        } else {
          r = TC_ERR_INTERNAL;
        }
      }
    }
    events_.push(TC_EVENT_BEAT_LOADED, r, durationMs);
  });
  return TC_OK;
}

tc_result Engine::play() noexcept {
  if (!player_.hasTrack()) return TC_ERR_INVALID_STATE;
  player_.play();
  return TC_OK;
}

tc_result Engine::pause() noexcept {
  player_.pause();
  return TC_OK;
}

tc_result Engine::seekMs(double ms) noexcept {
  if (!player_.hasTrack()) return TC_ERR_INVALID_STATE;
  if (!(ms >= 0.0)) return TC_ERR_INVALID_ARG;
  const auto frame = static_cast<uint64_t>(ms * config_.sampleRate / 1000.0);
  player_.seekFrames(frame);
  // Đang đổi tone: báo luồng render ưu tiên vùng vừa tua tới (nếu chưa có).
  if (requestedSemis_.load(std::memory_order_relaxed) != 0) {
    worker_.post([this, frame] {
      if (requestedTrack_) {
        requestedTrack_->wantFrame.store(static_cast<int64_t>(frame), std::memory_order_relaxed);
      }
    });
  }
  return TC_OK;
}

tc_result Engine::setTranspose(int32_t semitones) {
  if (semitones < -6 || semitones > 6) return TC_ERR_INVALID_ARG;
  if (!player_.hasTrack()) return TC_ERR_INVALID_STATE;
  if (requestedSemis_.exchange(semitones, std::memory_order_relaxed) == semitones) return TC_OK;
  const uint32_t gen = transposeGen_.fetch_add(1, std::memory_order_relaxed) + 1;
  transposeProgress_.store(semitones == 0 ? 1.0f : 0.0f, std::memory_order_relaxed);
  worker_.post([this, semitones, gen] {
    if (gen != transposeGen_.load(std::memory_order_relaxed)) return;  // đã có yêu cầu mới hơn
    if (requestedTrack_) requestedTrack_->cancelled.store(true, std::memory_order_relaxed);
    requestedTrack_.reset();
    if (!currentTrack_) return;
    if (semitones == 0) {
      player_.setTransposed(nullptr);
      return;
    }
    auto tt = std::make_shared<TransposedTrack>(currentTrack_, semitones);
    requestedTrack_ = tt;
    player_.setTransposed(tt);
    BeatRenderer::start(
        worker_, tt, player_.positionFrames(), config_.sampleRate,
        [this, gen](float p) {
          if (gen == transposeGen_.load(std::memory_order_relaxed)) {
            transposeProgress_.store(p, std::memory_order_relaxed);
          }
        },
        [this, gen](int semis, double ms) {
          if (gen != transposeGen_.load(std::memory_order_relaxed)) return;
          transposeProgress_.store(1.0f, std::memory_order_relaxed);
          events_.push(TC_EVENT_TRANSPOSE_READY, semis, ms);
        });
  });
  return TC_OK;
}

void Engine::fillPlayback(tc_playback_state& s) const noexcept {
  const double toMs = 1000.0 / config_.sampleRate;
  s.has_beat = player_.hasTrack() ? 1 : 0;
  s.playing = player_.isPlaying() ? 1 : 0;
  s.position_ms = static_cast<double>(player_.positionFrames()) * toMs;
  s.duration_ms = static_cast<double>(player_.durationFrames()) * toMs;
  s.transpose = requestedSemis_.load(std::memory_order_relaxed);
  s.transpose_progress = transposeProgress_.load(std::memory_order_relaxed);
  s.audible_transpose = player_.audibleSemitones();
}

// ---------------------------------------------------------------------------
// Route & sự kiện từ hệ điều hành

tc_result Engine::setRouteHint(int32_t route) {
  if (route < TC_ROUTE_UNKNOWN || route > TC_ROUTE_RECEIVER) return TC_ERR_INVALID_ARG;
  onRouteChanged(route);
  return TC_OK;
}

void Engine::onRouteChanged(int32_t route) {
  const int32_t old = route_.exchange(route, std::memory_order_relaxed);
  updateMonitorTarget();
  if (old == route) return;
  events_.push(TC_EVENT_ROUTE_CHANGED, route);
  if (route == TC_ROUTE_BLUETOOTH && monitorEnabled_.load(std::memory_order_relaxed)) {
    events_.push(TC_EVENT_MONITOR_BLOCKED);
  }
}

void Engine::onInterruption(bool began) {
  events_.push(TC_EVENT_INTERRUPTED, began ? 1 : 0);
}

void Engine::onStreamRestarted(tc_result result) {
  events_.push(TC_EVENT_STREAM_RESTARTED, result);
}

// ---------------------------------------------------------------------------
// Đo độ trễ

tc_result Engine::startLatencyMeasurement() {
  if (state() != TC_STATE_RUNNING) return TC_ERR_INVALID_STATE;
  std::lock_guard<std::mutex> lock(latencyMutex_);
  if (probe_.isActive() || latencyAnalyzing_) return TC_ERR_INVALID_STATE;
  latencyInfo_ = backend_ ? backend_->info() : BackendInfo{};
  latencyInfo_.route = route_.load(std::memory_order_relaxed);
  probe_.arm();
  return TC_OK;
}

void Engine::checkLatencyCompletion() {
  std::lock_guard<std::mutex> lock(latencyMutex_);
  if (!probe_.isComplete() || latencyAnalyzing_) return;
  latencyAnalyzing_ = true;
  // Phân tích tốn ~0,1–1 s CPU → chạy ở luồng nền. Probe giữ trạng thái "complete" nên
  // audio thread không ghi đè dữ liệu trong lúc phân tích; arm() bị chặn bởi latencyAnalyzing_.
  worker_.post([this] {
    const LatencyProbe::Result r = probe_.analyze();
    std::lock_guard<std::mutex> lk(latencyMutex_);
    tc_latency_result& out = latencyResult_;
    out = tc_latency_result{};
    out.struct_size = sizeof(tc_latency_result);
    out.valid = r.valid ? 1 : 0;
    out.latency_ms = r.latencyMs;
    out.latency_frames = r.latencyFrames;
    out.confidence = r.confidence;
    out.valid_bursts = r.validBursts;
    out.total_bursts = static_cast<int32_t>(r.bursts.size());
    out.spread_ms = 1000.0 * r.spreadFrames / config_.sampleRate;
    out.reported_input_latency_ms = latencyInfo_.inputLatencyMs;
    out.reported_output_latency_ms = latencyInfo_.outputLatencyMs;
    out.buffer_frames = latencyInfo_.bufferFrames;
    out.burst_frames = latencyInfo_.burstFrames;
    out.route = latencyInfo_.route;
    latencyAnalyzing_ = false;
    probe_.cancel();  // về trạng thái nghỉ, cho phép đo lần sau
    events_.push(TC_EVENT_LATENCY_DONE, r.valid ? TC_OK : TC_ERR_INTERNAL,
                 r.valid ? r.latencyMs : 0.0);
  });
}

tc_result Engine::latencyResult(tc_latency_result& out) const {
  std::lock_guard<std::mutex> lock(latencyMutex_);
  const uint32_t size = std::min<uint32_t>(out.struct_size, sizeof(tc_latency_result));
  std::memcpy(&out, &latencyResult_, size);
  out.struct_size = size;
  return TC_OK;
}

std::size_t Engine::pollEvents(tc_event* out, std::size_t max) {
  if (player_.consumeEnded()) events_.push(TC_EVENT_BEAT_ENDED);
  // Key áp dụng đổi: theo timeline (audio thread tăng bộ đếm) hoặc beat bắt đầu nghe ở tone mới.
  const uint32_t seq = autoKeySeq_.load(std::memory_order_acquire);
  const int audible = player_.audibleSemitones();
  if (seq != autoKeySeen_ || audible != lastAudible_) {
    autoKeySeen_ = seq;
    lastAudible_ = audible;
    int32_t root = 0;
    int32_t scale = 0;
    appliedKey(root, scale);
    events_.push(TC_EVENT_KEY_CHANGED, root, static_cast<double>(scale));
  }
  collectTimelineGarbage();
  checkLatencyCompletion();
  // Track cũ (vài chục MB) được huỷ ở luồng nền – cùng luồng với setTrack().
  if (player_.hasGarbage()) worker_.post([this] { player_.collectGarbage(); });
  fxRack_.collectGarbage();  // chuỗi FX cũ đã ngân xong (luồng điều khiển)
  return events_.pop(out, max);
}

// ---------------------------------------------------------------------------
// Audio thread

void Engine::process(const float* input, float* output, uint32_t frames) noexcept {
  processAudio(input, config_.inputChannels, output, config_.outputChannels, frames);
}

// [RT] Không cấp phát, không khóa, không I/O.
void Engine::processAudio(const float* in, uint32_t inChannels, float* out, uint32_t outChannels,
                          uint32_t frames) noexcept {
  ScopedFlushDenormals ftz;
  const auto t0 = std::chrono::steady_clock::now();
  inputChannelsSeen_.store(inChannels, std::memory_order_relaxed);

  // Thiết bị có thể gọi với block lớn hơn buffer đã cấp phát → chia nhỏ.
  uint32_t done = 0;
  while (done < frames) {
    const uint32_t n = std::min(frames - done, config_.maxBlockFrames);
    processChunk(in != nullptr ? in + static_cast<std::size_t>(done) * inChannels : nullptr,
                 inChannels, out + static_cast<std::size_t>(done) * outChannels, outChannels, n);
    done += n;
  }

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double blockSec = static_cast<double>(frames) / config_.sampleRate;
  if (blockSec > 0.0) {
    const auto load = static_cast<float>(elapsed / blockSec);
    cpuLoad_.store(load, std::memory_order_relaxed);
    // Đỉnh theo cửa sổ 1 giây: phát hiện block tốn bất thường dễ gây xrun.
    cpuPeakAccum_ = std::max(cpuPeakAccum_, load);
    cpuPeakFrames_ += frames;
    if (cpuPeakFrames_ >= config_.sampleRate) {
      cpuPeak_.store(cpuPeakAccum_, std::memory_order_relaxed);
      cpuPeakAccum_ = 0.0f;
      cpuPeakFrames_ = 0;
    }
  }
  framesProcessed_.fetch_add(frames, std::memory_order_relaxed);
  callbacks_.fetch_add(1, std::memory_order_relaxed);
}

// [RT] Nhận timeline mới (nếu có) và đặt key theo đoạn chứa vị trí beat hiện tại.
void Engine::applyKeyTimeline() noexcept {
  KeyTimeline* t = nullptr;
  while (timelineIncoming_.pop(t)) {
    if (timeline_ != nullptr && !timelineRetired_.push(timeline_)) {
      // Hàng đợi trả về đầy (luồng điều khiển chưa dọn): giữ timeline cũ, trả timeline mới về
      // để không phải giải phóng trong audio thread. Không xảy ra với nhịp dùng bình thường.
      timelineRetired_.push(t);
      continue;
    }
    timeline_ = t;
    appliedSegment_ = -1;
  }
  const bool follow = followTimeline_.load(std::memory_order_acquire) && timeline_ != nullptr &&
                      !timeline_->entries.empty();
  if (!follow) {
    if (keyOverride_) {
      pitch_.setKeyOverride(false, 0, Scale::Chromatic);
      keyOverride_ = false;
      appliedSegment_ = -1;
    }
    return;
  }
  // Đoạn cuối cùng có startFrame ≤ vị trí (trước đoạn đầu → dùng đoạn đầu).
  const auto& e = timeline_->entries;
  const uint64_t pos = player_.positionFrames();
  const auto it =
      std::upper_bound(e.begin(), e.end(), pos,
                       [](uint64_t p, const KeyTimeline::Entry& x) { return p < x.startFrame; });
  const int idx = it == e.begin() ? 0 : static_cast<int>(it - e.begin()) - 1;
  if (keyOverride_ && idx == appliedSegment_) return;
  appliedSegment_ = idx;
  keyOverride_ = true;
  const KeyTimeline::Entry& seg = e[static_cast<std::size_t>(idx)];
  pitch_.setKeyOverride(true, seg.root, seg.scale);
  autoKey_.store(packKey(seg.root, seg.scale), std::memory_order_relaxed);
  autoKeySeq_.fetch_add(1, std::memory_order_release);
}

// [RT]
void Engine::processChunk(const float* in, uint32_t inChannels, float* out, uint32_t outChannels,
                          uint32_t frames) noexcept {
  float* mic = micBuf_.data();
  for (uint32_t i = 0; i < frames; ++i) mic[i] = in != nullptr ? in[i * inChannels] : 0.0f;

  // Đang đo độ trễ: chỉ phát tín hiệu kích thích.
  float* stim = stimBuf_.data();
  if (probe_.processBlock(mic, stim, frames)) {
    for (uint32_t i = 0; i < frames; ++i) {
      for (uint32_t c = 0; c < outChannels; ++c) out[i * outChannels + c] = stim[i];
    }
    probeWasActive_ = true;
    return;
  }
  if (probeWasActive_) {
    // Đo xong: trong limiter còn 1 ms âm thanh từ trước lúc đo → bỏ.
    limiter_.reset();
    probeWasActive_ = false;
  }

  // Giọng: lọc thông cao, dò cao độ, tự chỉnh giọng (tại chỗ) — key theo timeline nếu có.
  applyKeyTimeline();
  pitch_.process(mic, frames);

  float* beat = beatBuf_.data();
  player_.render(beat, frames);
  // Beat bắt đầu nghe ở tone mới → key chỉnh giọng theo (từ block sau).
  const int audible = player_.audibleSemitones();
  if (audible != pitchTranspose_) {
    pitchTranspose_ = audible;
    pitch_.setTranspose(audible);
  }

  // FX giọng: khô (EQ, nén, de-esser) + ướt (delay, reverb) → stereo. Luôn chạy (cả khi tắt nghe
  // lại) để đuôi vang liền mạch khi bật lại; preset "Khô" thì đi thẳng.
  const auto fx0 = std::chrono::steady_clock::now();
  float* vl = voiceL_.data();
  float* vr = voiceR_.data();
  fxRack_.process(mic, vl, vr, frames);
  const double fxSec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - fx0).count();
  fxLoad_.store(static_cast<float>(fxSec * config_.sampleRate / static_cast<double>(frames)),
                std::memory_order_relaxed);

  float* mix = mixBuf_.data();
  for (uint32_t i = 0; i < frames; ++i) {
    const float mon = monitorGain_.next() * monitorMix_.next();
    const float bg = beatGain_.next();
    const float mg = masterGain_.next();
    mix[2 * i] = (beat[2 * i] * bg + vl[i] * mon) * mg;
    mix[2 * i + 1] = (beat[2 * i + 1] * bg + vr[i] * mon) * mg;
  }
  // Limiter: đỉnh vượt trần (giọng to + beat to + gain) được hạ mượt thay vì kẹp méo.
  float* lim = outBuf_.data();
  limiter_.process(mix, lim, frames);
  for (uint32_t i = 0; i < frames; ++i) {
    float* o = out + static_cast<std::size_t>(i) * outChannels;
    if (outChannels == 1) {
      o[0] = 0.5f * (lim[2 * i] + lim[2 * i + 1]);
    } else {
      o[0] = lim[2 * i];
      o[1] = lim[2 * i + 1];
      for (uint32_t c = 2; c < outChannels; ++c) o[c] = 0.0f;
    }
  }
}

// ---------------------------------------------------------------------------
// Đo lường

void Engine::fillMetrics(tc_metrics& m) const noexcept {
  m.state = state();
  m.sample_rate = config_.sampleRate;
  m.frames_processed = framesProcessed_.load(std::memory_order_relaxed);
  m.callbacks = callbacks_.load(std::memory_order_relaxed);
  m.cpu_load = cpuLoad_.load(std::memory_order_relaxed);
  m.route = route_.load(std::memory_order_relaxed);
  m.monitor_active = monitorMix_.target() > 0.5f ? 1 : 0;
  m.input_channels = static_cast<int32_t>(inputChannelsSeen_.load(std::memory_order_relaxed));
  m.cpu_load_peak =
      std::max(cpuPeak_.load(std::memory_order_relaxed), cpuLoad_.load(std::memory_order_relaxed));
  m.algorithm_latency_frames = pitch_.algorithmLatency() + limiter_.lookahead();
  m.fx_load = fxLoad_.load(std::memory_order_relaxed);
  m.fx_chains = fxRack_.audioChains();
  if (backend_) {
    const BackendInfo info = backend_->info();
    m.xruns = backend_->xruns();
    m.buffer_frames = info.bufferFrames;
    m.burst_frames = info.burstFrames;
    m.reported_input_latency_ms = info.inputLatencyMs;
    m.reported_output_latency_ms = info.outputLatencyMs;
    m.audio_api = info.api;
  } else {
    m.xruns = 0;
    m.buffer_frames = config_.maxBlockFrames;
    m.burst_frames = 0;
    m.reported_input_latency_ms = 0.0;
    m.reported_output_latency_ms = 0.0;
    m.audio_api = TC_AUDIO_API_NONE;
  }
}

}  // namespace tonecore
