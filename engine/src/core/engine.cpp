#include "core/engine.h"

#include <chrono>

#include "core/denormal.h"

namespace tonecore {

namespace {
// Thời gian làm mượt tham số gain (giây).
constexpr double kGainSmoothingSec = 0.02;
}  // namespace

Engine::Engine(const EngineConfig& config) : config_(config) {
  masterGain_.prepare(config_.sampleRate, kGainSmoothingSec);
}

tc_result Engine::start() noexcept {
  tc_engine_state expected = TC_STATE_STOPPED;
  if (!state_.compare_exchange_strong(expected, TC_STATE_RUNNING, std::memory_order_acq_rel)) {
    return TC_ERR_INVALID_STATE;
  }
  return TC_OK;
}

tc_result Engine::stop() noexcept {
  tc_engine_state expected = TC_STATE_RUNNING;
  if (!state_.compare_exchange_strong(expected, TC_STATE_STOPPED, std::memory_order_acq_rel)) {
    return TC_ERR_INVALID_STATE;
  }
  return TC_OK;
}

tc_result Engine::setParam(int32_t nodeId, int32_t paramId, float value) noexcept {
  if (nodeId == TC_NODE_MASTER && paramId == TC_PARAM_GAIN) {
    masterGain_.setTarget(value);
    return TC_OK;
  }
  return TC_ERR_INVALID_ARG;
}

tc_result Engine::getParam(int32_t nodeId, int32_t paramId, float* outValue) const noexcept {
  if (nodeId == TC_NODE_MASTER && paramId == TC_PARAM_GAIN) {
    *outValue = masterGain_.target();
    return TC_OK;
  }
  return TC_ERR_INVALID_ARG;
}

// [RT] Không cấp phát, không khóa, không I/O.
void Engine::process(const float* input, float* output, uint32_t frames) noexcept {
  ScopedFlushDenormals ftz;
  const auto t0 = std::chrono::steady_clock::now();

  const uint32_t inCh = config_.inputChannels;
  const uint32_t outCh = config_.outputChannels;

  for (uint32_t i = 0; i < frames; ++i) {
    // M0: lấy kênh 0 của input (micro mono), nhân gain, nhân bản ra mọi kênh output.
    const float x = input != nullptr ? input[i * inCh] : 0.0f;
    const float y = x * masterGain_.next();
    for (uint32_t c = 0; c < outCh; ++c) output[i * outCh + c] = y;
  }

  // Tải CPU = thời gian xử lý / thời lượng block.
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double blockSec = static_cast<double>(frames) / config_.sampleRate;
  if (blockSec > 0.0)
    cpuLoad_.store(static_cast<float>(elapsed / blockSec), std::memory_order_relaxed);

  framesProcessed_.fetch_add(frames, std::memory_order_relaxed);
  callbacks_.fetch_add(1, std::memory_order_relaxed);
}

void Engine::fillMetrics(tc_metrics& m) const noexcept {
  m.state = state();
  m.sample_rate = config_.sampleRate;
  m.frames_processed = framesProcessed_.load(std::memory_order_relaxed);
  m.callbacks = callbacks_.load(std::memory_order_relaxed);
  m.xruns = xruns_.load(std::memory_order_relaxed);
  m.cpu_load = cpuLoad_.load(std::memory_order_relaxed);
}

}  // namespace tonecore
