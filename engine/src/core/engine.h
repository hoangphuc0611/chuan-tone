#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/event_queue.h"
#include "core/smoothed_param.h"
#include "core/worker.h"
#include "io/audio_backend.h"
#include "latency/latency_probe.h"
#include "pitch/voice_pitch_node.h"
#include "player/beat_player.h"
#include "tonecore/tonecore.h"

namespace tonecore {

struct EngineConfig {
  uint32_t sampleRate = 48000;
  uint32_t maxBlockFrames = 1024;
  uint32_t inputChannels = 1;
  uint32_t outputChannels = 2;
  tc_backend backend = TC_BACKEND_NULL;
};

// Lõi engine.
//
// Chuỗi xử lý (mỗi block, trong audio thread):
//   mic (kênh 0) → VoicePitchNode (HPF, dò cao độ, tự chỉnh giọng) ─× monitorGain × monitorMix ─┐
//   beat (stereo) ─× beatGain ────────────────┼─ cộng → × masterGain → kẹp ±1 → loa/tai nghe
//   (khi đang đo độ trễ: chỉ phát tín hiệu MLS, tắt mọi nguồn khác)
//
// monitorMix chuyển dần 0↔1 (10 ms) khi bật/tắt nghe lại hoặc khi route đổi sang Bluetooth.
class Engine : public BackendHost {
 public:
  explicit Engine(const EngineConfig& config);
  ~Engine() override;

  const EngineConfig& config() const noexcept { return config_; }

  // ---- Luồng điều khiển ----
  tc_result start();
  tc_result stop();
  tc_engine_state state() const noexcept { return state_.load(std::memory_order_acquire); }

  tc_result setParam(int32_t nodeId, int32_t paramId, float value) noexcept;
  tc_result getParam(int32_t nodeId, int32_t paramId, float* outValue) const noexcept;

  tc_result loadBeat(const std::string& path);
  tc_result play() noexcept;
  tc_result pause() noexcept;
  tc_result seekMs(double ms) noexcept;
  void fillPlayback(tc_playback_state& s) const noexcept;

  tc_result setRouteHint(int32_t route);

  // Tự chỉnh giọng
  tc_result setKey(int32_t root, int32_t scale) noexcept;
  void key(int32_t& root, int32_t& scale) const noexcept;
  tc_result setCorrectionPreset(int32_t preset) noexcept;
  void fillPitchState(tc_pitch_state& s) const noexcept;
  tc_result startLatencyMeasurement();
  tc_result latencyResult(tc_latency_result& out) const;

  // Lấy sự kiện; đồng thời chuyển các cờ từ audio thread thành sự kiện.
  std::size_t pollEvents(tc_event* out, std::size_t max);

  void fillMetrics(tc_metrics& m) const noexcept;

  // Chờ luồng nền làm xong mọi việc (test).
  void waitForBackgroundWork() { worker_.waitIdle(); }

  // ---- Audio thread ----
  // Xử lý theo cấu hình kênh của EngineConfig (dùng cho backend NULL / xử lý offline).
  void process(const float* input, float* output, uint32_t frames) noexcept;

  // BackendHost
  void processAudio(const float* in, uint32_t inChannels, float* out, uint32_t outChannels,
                    uint32_t frames) noexcept override;
  void onRouteChanged(int32_t route) override;
  void onInterruption(bool began) override;
  void onStreamRestarted(tc_result result) override;

 private:
  void processChunk(const float* in, uint32_t inChannels, float* out, uint32_t outChannels,
                    uint32_t frames) noexcept;
  void updateMonitorTarget() noexcept;
  void checkLatencyCompletion();

  EngineConfig config_;
  std::atomic<tc_engine_state> state_{TC_STATE_STOPPED};

  // Tham số
  SmoothedParam masterGain_{1.0f, 0.0f, 4.0f};
  SmoothedParam monitorGain_{1.0f, 0.0f, 4.0f};
  SmoothedParam beatGain_{1.0f, 0.0f, 4.0f};
  SmoothedParam monitorMix_{0.0f, 0.0f, 1.0f};
  std::atomic<bool> monitorEnabled_{false};
  std::atomic<int32_t> route_{TC_ROUTE_UNKNOWN};

  // Thành phần
  BeatPlayer player_;
  VoicePitchNode pitch_;
  LatencyProbe probe_;
  std::unique_ptr<AudioBackend> backend_;
  EventQueue events_;

  // Buffer cấp phát trước cho audio thread
  std::vector<float> micBuf_;
  std::vector<float> beatBuf_;
  std::vector<float> stimBuf_;

  // Đo độ trễ (luồng điều khiển + luồng nền)
  mutable std::mutex latencyMutex_;
  tc_latency_result latencyResult_{};
  bool latencyAnalyzing_ = false;  // bảo vệ bởi latencyMutex_
  BackendInfo latencyInfo_{};      // thông tin thiết bị lúc bắt đầu đo

  // Đo lường: audio thread ghi (relaxed), UI đọc.
  std::atomic<uint64_t> framesProcessed_{0};
  std::atomic<uint64_t> callbacks_{0};
  std::atomic<float> cpuLoad_{0.0f};
  std::atomic<float> cpuPeak_{0.0f};  // đỉnh của cửa sổ 1 giây gần nhất đã xong
  float cpuPeakAccum_ = 0.0f;         // audio thread
  uint64_t cpuPeakFrames_ = 0;        // audio thread
  std::atomic<uint32_t> inputChannelsSeen_{0};

  // Luồng nền – khai báo cuối để bị huỷ đầu tiên (join trước khi các thành viên khác mất).
  Worker worker_;
};

}  // namespace tonecore
