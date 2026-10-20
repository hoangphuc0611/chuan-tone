#pragma once

#include <atomic>
#include <cstdint>

#include "core/smoothed_param.h"
#include "tonecore/tonecore.h"

namespace tonecore {

struct EngineConfig {
  uint32_t sampleRate = 48000;
  uint32_t maxBlockFrames = 1024;
  uint32_t inputChannels = 1;
  uint32_t outputChannels = 2;
  tc_backend backend = TC_BACKEND_NULL;
};

// Lõi engine. Ở M0 chỉ có: vòng đời, một tham số master gain có làm mượt, xử lý offline
// (micro mono → nhân bản ra các kênh output) và bộ đếm đo lường.
// Đồ thị node đầy đủ sẽ thêm từ M1.
class Engine {
 public:
  explicit Engine(const EngineConfig& config);

  const EngineConfig& config() const noexcept { return config_; }

  tc_result start() noexcept;
  tc_result stop() noexcept;
  tc_engine_state state() const noexcept { return state_.load(std::memory_order_acquire); }

  tc_result setParam(int32_t nodeId, int32_t paramId, float value) noexcept;
  tc_result getParam(int32_t nodeId, int32_t paramId, float* outValue) const noexcept;

  // [RT] Xử lý một block. Đây cũng là hàm mà backend thật (Oboe/RemoteIO) sẽ gọi ở M1.
  void process(const float* input, float* output, uint32_t frames) noexcept;

  void fillMetrics(tc_metrics& m) const noexcept;

 private:
  EngineConfig config_;
  std::atomic<tc_engine_state> state_{TC_STATE_STOPPED};
  SmoothedParam masterGain_{1.0f, 0.0f, 4.0f};

  // Bộ đếm đo lường: audio thread ghi (relaxed), UI đọc.
  std::atomic<uint64_t> framesProcessed_{0};
  std::atomic<uint64_t> callbacks_{0};
  std::atomic<uint32_t> xruns_{0};
  std::atomic<float> cpuLoad_{0.0f};
};

}  // namespace tonecore
