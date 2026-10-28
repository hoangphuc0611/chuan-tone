#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/event_queue.h"
#include "core/smoothed_param.h"
#include "core/spsc_ring.h"
#include "core/worker.h"
#include "fx/peak_limiter.h"
#include "io/audio_backend.h"
#include "latency/latency_probe.h"
#include "pitch/voice_pitch_node.h"
#include "player/beat_player.h"
#include "player/beat_renderer.h"
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
//   beat (stereo) ─× beatGain ────────────────┼─ cộng → × masterGain → limiter (nhìn trước 1 ms) →
//   ra (khi đang đo độ trễ: chỉ phát tín hiệu MLS, tắt mọi nguồn khác)
//
// monitorMix chuyển dần 0↔1 (10 ms) khi bật/tắt nghe lại hoặc khi route đổi sang Bluetooth.
//
// Theo tone của bài (từ v0.4): timeline key được dựng ở luồng điều khiển rồi chuyển con trỏ sang
// audio thread qua hàng đợi SPSC (giống cách thay track của BeatPlayer). Đầu mỗi block audio
// thread tìm đoạn chứa vị trí beat hiện tại; sang đoạn mới thì đặt key override cho
// VoicePitchNode và tăng bộ đếm → pollEvents phát TC_EVENT_KEY_CHANGED. Timeline cũ được trả
// về hàng đợi khác và huỷ ở luồng điều khiển.
//
// Đổi tone beat (từ v0.5, ADR-004): tc_set_transpose → luồng nền tạo TransposedTrack, giao cho
// BeatPlayer và render dần từ vị trí đang phát (BeatRenderer). Khi bản mới bắt đầu được nghe,
// audio thread cộng số nửa cung vào key chỉnh giọng (VoicePitchNode::setTranspose) — key chọn tay
// và timeline vẫn là tone của beat gốc. Nạp beat mới → tone về 0.
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
  // Đổi tone beat −6…+6 nửa cung (render nền; nghe sau ~0,1 s). 0 = beat gốc.
  tc_result setTranspose(int32_t semitones);
  int32_t transpose() const noexcept { return requestedSemis_.load(std::memory_order_relaxed); }
  void fillPlayback(tc_playback_state& s) const noexcept;

  tc_result setRouteHint(int32_t route);

  // Tự chỉnh giọng
  tc_result setKey(int32_t root, int32_t scale) noexcept;
  void key(int32_t& root, int32_t& scale) const noexcept;
  // Key đang dùng để chỉnh giọng = key beat gốc (tay hoặc timeline) + số nửa cung đang nghe.
  void appliedKey(int32_t& root, int32_t& scale) const noexcept;
  tc_result setCorrectionPreset(int32_t preset) noexcept;
  tc_result setTuning(float cents) noexcept;
  float tuning() const noexcept { return pitch_.tuningCents(); }
  tc_result setKeyTimeline(const tc_key_segment* segments, uint32_t count);
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
  void applyKeyTimeline() noexcept;
  void collectTimelineGarbage();
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

  // Timeline key (từ v0.4)
  struct KeyTimeline {
    struct Entry {
      uint64_t startFrame;
      int32_t root;
      Scale scale;
    };
    std::vector<Entry> entries;  // tăng dần theo startFrame
  };
  static int32_t packKey(int32_t root, Scale scale) noexcept {
    return root | (static_cast<int32_t>(scale) << 8);
  }
  SpscRing<KeyTimeline*> timelineIncoming_{8};
  SpscRing<KeyTimeline*> timelineRetired_{16};  // > incoming → không đầy giữa 2 lần dọn
  KeyTimeline* timeline_ = nullptr;             // audio thread (và destructor)
  int appliedSegment_ = -1;                     // audio thread
  bool keyOverride_ = false;                    // audio thread
  std::atomic<bool> followTimeline_{false};
  std::atomic<int32_t> autoKey_{0};      // packKey của key đang theo timeline
  std::atomic<uint32_t> autoKeySeq_{0};  // audio thread tăng mỗi lần tự đổi key
  uint32_t autoKeySeen_ = 0;             // luồng điều khiển
  int lastAudible_ = 0;                  // luồng điều khiển: tone đang nghe đã báo qua sự kiện
  int pitchTranspose_ = 0;               // audio thread: tone đã áp cho VoicePitchNode

  // Đổi tone beat (từ v0.5).
  std::atomic<int32_t> requestedSemis_{0};
  std::atomic<uint32_t> transposeGen_{0};  // tăng mỗi lần đổi tone / nạp beat
  std::atomic<float> transposeProgress_{1.0f};
  // Chỉ luồng nền (Worker) dùng.
  std::shared_ptr<const Track> currentTrack_;
  std::shared_ptr<TransposedTrack> requestedTrack_;

  // Thành phần
  BeatPlayer player_;
  VoicePitchNode pitch_;
  LatencyProbe probe_;
  // Limiter đầu ra (từ M5): nhìn trước 1 ms, trần 0,98 (−0,18 dBFS), hai kênh chung một hệ số;
  // dưới trần đi thẳng (chỉ trễ 1 ms). Thay chỗ kẹp ±1.
  PeakLimiter limiter_;
  bool probeWasActive_ = false;  // audio thread: vừa đo độ trễ xong → xoá limiter (bỏ âm cũ)
  std::unique_ptr<AudioBackend> backend_;
  EventQueue events_;

  // Buffer cấp phát trước cho audio thread
  std::vector<float> micBuf_;
  std::vector<float> beatBuf_;
  std::vector<float> stimBuf_;
  std::vector<float> mixBuf_;  // stereo interleaved trước limiter
  std::vector<float> outBuf_;  // stereo interleaved sau limiter

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
