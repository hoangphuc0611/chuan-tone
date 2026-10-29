#pragma once

#include <cstdint>

namespace tonecore {

struct GateParams {
  float thresholdDb = -50.0f;  // mở khi bao biên ≥ ngưỡng (dBFS)
  float hysteresisDb = 6.0f;   // đóng khi bao biên < ngưỡng − trễ (chống "nháy")
  float attackMs = 1.0f;       // mở: hằng thời gian của hệ số
  float holdMs = 80.0f;        // giữ mở thêm sau khi tiếng nhỏ xuống dưới ngưỡng đóng
  float releaseMs = 150.0f;    // đóng: hằng thời gian của hệ số
  float rangeDb = -30.0f;      // khi đóng chỉ giảm chừng này (không tắt hẳn)
};

// Cổng chặn tiếng ồn cho giọng (CLAUDE.md 3.7), mono, tại chỗ, không trễ (Zölzer, DAFX ch. 4):
//  1. Bao biên đỉnh: e ← max(|x|, e·d), d ứng với τ = 20 ms (bắt kịp ngay tiếng bật, nhả từ từ).
//  2. Trạng thái có trễ: đóng → mở khi e ≥ ngưỡng; đang mở thì mỗi mẫu e ≥ ngưỡng − trễ lại nạp đủ
//     thời gian giữ; hết thời gian giữ mới đóng → tiếng dao động quanh ngưỡng không làm cổng nháy.
//  3. Hệ số g tiến về 1 (mở, τ attack) hoặc 10^(range/20) (đóng, τ release); y = g·x. Khi đóng chỉ
//     giảm −30 dB thay vì tắt hẳn: phụ âm cuối nhẹ của tiếng Việt (hơi, dấu hỏi / ngã) không bị cụt
//     hẳn nếu cổng đóng sớm.
// Cấp phát: không.
class NoiseGate {
 public:
  explicit NoiseGate(double sampleRate);

  void setParams(const GateParams& p) noexcept;  // [RT]
  void reset() noexcept;
  // [RT] Coi như đang mở (bắt đầu giữ): bật gate giữa câu hát không cắt cụt tiếng đang hát.
  void forceOpen() noexcept;
  void process(float* x, uint32_t n) noexcept;  // [RT]

  bool isOpen() const noexcept { return open_; }
  float gain() const noexcept { return gain_; }
  static constexpr double kDetectorSec = 0.020;

 private:
  double fs_;
  float openLevel_ = 0.0f;
  float closeLevel_ = 0.0f;
  float floor_ = 0.0f;
  float envDecay_ = 0.0f;
  float attack_ = 1.0f;
  float release_ = 1.0f;
  int32_t holdSamples_ = 0;

  float env_ = 0.0f;
  float gain_ = 0.0f;
  bool open_ = false;
  int32_t hold_ = 0;
};

}  // namespace tonecore
