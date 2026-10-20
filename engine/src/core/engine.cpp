#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/denormal.h"
#include "decode/decoder.h"

namespace tonecore {

namespace {
constexpr double kGainSmoothingSec = 0.02;
constexpr double kMonitorFadeSec = 0.01;
}  // namespace

Engine::Engine(const EngineConfig& config)
    : config_(config),
      player_(config.sampleRate),
      probe_(config.sampleRate),
      micBuf_(config.maxBlockFrames, 0.0f),
      beatBuf_(2 * static_cast<std::size_t>(config.maxBlockFrames), 0.0f),
      stimBuf_(config.maxBlockFrames, 0.0f) {
  masterGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  monitorGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  beatGain_.prepare(config_.sampleRate, kGainSmoothingSec);
  monitorMix_.prepare(config_.sampleRate, kMonitorFadeSec);
  latencyResult_.struct_size = sizeof(tc_latency_result);
}

Engine::~Engine() {
  if (state() == TC_STATE_RUNNING) stop();
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
  return TC_ERR_INVALID_ARG;
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
      std::unique_ptr<Track> track = prepareTrack(std::move(decoded), rate);
      if (!track) {
        r = TC_ERR_NOT_SUPPORTED;
      } else {
        durationMs = 1000.0 * static_cast<double>(track->frames) / rate;
        if (!player_.setTrack(std::move(track))) r = TC_ERR_INTERNAL;
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
  player_.seekFrames(static_cast<uint64_t>(ms * config_.sampleRate / 1000.0));
  return TC_OK;
}

void Engine::fillPlayback(tc_playback_state& s) const noexcept {
  const double toMs = 1000.0 / config_.sampleRate;
  s.has_beat = player_.hasTrack() ? 1 : 0;
  s.playing = player_.isPlaying() ? 1 : 0;
  s.position_ms = static_cast<double>(player_.positionFrames()) * toMs;
  s.duration_ms = static_cast<double>(player_.durationFrames()) * toMs;
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
  checkLatencyCompletion();
  // Track cũ (vài chục MB) được huỷ ở luồng nền – cùng luồng với setTrack().
  if (player_.hasGarbage()) worker_.post([this] { player_.collectGarbage(); });
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
    cpuLoad_.store(static_cast<float>(elapsed / blockSec), std::memory_order_relaxed);
  }
  framesProcessed_.fetch_add(frames, std::memory_order_relaxed);
  callbacks_.fetch_add(1, std::memory_order_relaxed);
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
    return;
  }

  float* beat = beatBuf_.data();
  player_.render(beat, frames);

  for (uint32_t i = 0; i < frames; ++i) {
    const float voice = mic[i] * monitorGain_.next() * monitorMix_.next();
    const float bg = beatGain_.next();
    const float mg = masterGain_.next();
    // Kẹp ±1 chống vỡ tiếng; limiter thật sẽ thay thế ở M5.
    const float l = std::clamp((beat[2 * i] * bg + voice) * mg, -1.0f, 1.0f);
    const float r = std::clamp((beat[2 * i + 1] * bg + voice) * mg, -1.0f, 1.0f);
    float* o = out + static_cast<std::size_t>(i) * outChannels;
    if (outChannels == 1) {
      o[0] = 0.5f * (l + r);
    } else {
      o[0] = l;
      o[1] = r;
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
