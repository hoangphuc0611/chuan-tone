#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/spsc_ring.h"
#include "record/wav_file.h"
#include "tonecore/tonecore.h"

namespace tonecore {

// Thông tin lúc bắt đầu thu (luồng điều khiển điền), trả lại nguyên trong tc_record_status.
struct RecordStartInfo {
  double inputLatencyMs = 0.0;
  double outputLatencyMs = 0.0;
  uint32_t pitchLatencyFrames = 0;
  uint32_t outputLookaheadFrames = 0;
  int32_t route = TC_ROUTE_UNKNOWN;
};

// Bộ thu đa track (M7, CLAUDE.md mục 3.8): giọng khô (mono float32), giọng đã xử lý (stereo
// 16-bit), beat đang phát (stereo 16-bit) → dry.wav / wet.wav / beat.wav trong một thư mục.
//
// Audio thread gọi capture() mỗi block: xen kẽ 5 kênh vào buffer cấp sẵn rồi đẩy nguyên khối vào
// ring SPSC (≈ 4 s). Ring không đủ chỗ cho cả khối → bỏ cả khối, đếm frame rơi (5 kênh cùng rơi
// nên các track vẫn khớp nhau); không bao giờ chờ. Luồng ghi riêng rút ring ~10 ms một lần và
// ghi WAV (qua tên tạm, xem WavWriter).
//
// Trạng thái (atomic, chuyển bằng compare-exchange):
//   Idle/Done/Failed ─start()→ Armed ─(audio: block đầu, ghi mốc)→ Recording
//   Armed/Recording ─stop()→ StopRequested ─(audio xác nhận)→ Draining
//   Recording ─(đủ max frame, audio)→ Draining
//   Draining ─(luồng ghi rút hết ring, đóng file)→ Done / Failed
// Ring cấp phát một lần ở lần start() đầu tiên rồi giữ tới khi huỷ — audio thread chỉ chạm ring
// khi đang Recording, nên không bao giờ có lúc ring bị thay dưới tay nó.
class Recorder {
 public:
  Recorder(uint32_t sampleRate, uint32_t maxBlockFrames, double ringSeconds = 4.0);
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  // ---- Luồng điều khiển ----
  // Mở 3 file trong `dir` (phải có sẵn). Đang thu → TC_ERR_INVALID_STATE.
  tc_result start(const std::string& dir, double maxSeconds, const RecordStartInfo& info);
  // Dừng và chờ ghi xong. audioStopped = true khi chắc chắn không còn callback (engine đã dừng);
  // ngược lại chờ audio thread xác nhận (tối đa ~1 s, rồi tự chốt).
  tc_result stop(bool audioStopped);
  void fillStatus(tc_record_status& s) const noexcept;
  // Đang thu hoặc đang ghi nốt.
  bool busy() const noexcept;

  // Test: tạm dừng luồng ghi (để thử ring đầy).
  void pauseWriterForTest(bool paused) { writerPaused_.store(paused); }

  // ---- Audio thread ----
  // Có cần giọng khô của block này không (để engine khỏi chép khi không thu). [RT]
  bool wantsAudio() const noexcept {
    const int s = state_.load(std::memory_order_acquire);
    return s == kArmed || s == kRecording || s == kStopRequested;
  }
  // beat: stereo interleaved. beatFrame: vị trí beat đầu block; hostTimeNs: đồng hồ máy lúc
  // frame đầu block ra thiết bị (0 = không rõ). [RT]
  void capture(const float* dry, const float* wetL, const float* wetR, const float* beat,
               uint32_t frames, uint64_t beatFrame, int64_t hostTimeNs) noexcept;

 private:
  enum : int { kIdle = 0, kArmed, kRecording, kStopRequested, kDraining, kDone, kFailed };
  static constexpr uint32_t kChannels = 5;  // khô, xử lý L, R, beat L, R

  void writerLoop();
  void joinWriter();

  const uint32_t sampleRate_;
  const uint32_t maxBlockFrames_;
  const std::size_t ringFloats_;
  std::unique_ptr<SpscRing<float>> ring_;
  std::vector<float> interleave_;  // audio thread

  std::atomic<int> state_{kIdle};
  std::atomic<int32_t> result_{TC_OK};
  std::atomic<uint64_t> framesCaptured_{0};  // audio thread ghi
  std::atomic<uint64_t> framesWritten_{0};   // luồng ghi ghi
  std::atomic<uint64_t> dropped_{0};
  std::atomic<uint64_t> startBeatFrame_{0};
  std::atomic<int64_t> startHostTimeNs_{0};
  uint64_t maxFrames_ = 0;  // đặt trước khi Armed (release) → audio thread đọc sau acquire
  RecordStartInfo info_;

  WavWriter dry_;
  WavWriter wet_;
  WavWriter beat_;
  std::thread writer_;
  std::atomic<bool> writerPaused_{false};
};

}  // namespace tonecore
