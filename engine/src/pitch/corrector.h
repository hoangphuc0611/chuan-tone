#pragma once

#include <cstdint>

#include "dsp/biquad.h"
#include "pitch/f0_tracker.h"
#include "pitch/scales.h"

namespace tonecore {

struct CorrectorParams {
  float retuneMs = 15.0f;         // thời gian kéo về nốt đích (0 = tức thì, tối đa 200)
  float humanize = 0.5f;          // 0..1 — nốt ngân càng lâu thì chỉnh càng chậm
  float flexCents = 0.0f;         // lệch dưới ngưỡng này thì để nguyên (không chỉnh)
  float vibratoPreserve = 0.5f;   // 0..1 — phần rung giọng / luyến nhanh được giữ lại
  float hysteresisCents = 20.0f;  // vùng trễ khi chuyển nốt đích
  float maxCorrectionCents = 200.0f;
};

struct CorrectionFrame {
  bool active = false;      // đang có giọng để chỉnh
  int targetNote = -1;      // nốt đích (MIDI), -1 nếu chưa có
  float inputMidi = 0.0f;   // cao độ người hát (MIDI thực)
  float centsOff = 0.0f;    // người hát lệch bao nhiêu cent so với nốt đích (cho đồng hồ)
  float correction = 0.0f;  // lượng dịch cần áp (nửa cung); tỉ lệ = 2^(correction/12)
};

// Tính đường cong chỉnh cao độ, mỗi khung phân tích (hop 4 ms) một lần.
//
//  1. Tách cao độ m thành phần chậm s = LP_2Hz(m) (Butterworth bậc 2) và phần nhanh v = m − s
//     (rung giọng 4–8 Hz, luyến láy, dấu thanh).
//  2. "Tâm" để chọn nốt:  c = m − p·v  (p = vibratoPreserve). p = 1 → chỉ phần chậm được kéo
//     về nốt, phần nhanh giữ nguyên; p = 0 → kéo cả rung giọng (kiểu robot).
//  3. Nốt đích T = lượng tử c theo thang âm, có hysteresis.
//  4. Lượng chỉnh mong muốn d = T − c, bỏ qua nếu |d| < flex.
//  5. Làm mượt d bằng 2 tầng một cực nối tiếp (bậc 2) với τ = retune/4 → đạt ~90% sau `retune`.
//     humanize: nốt giữ lâu (> 120 ms) thì retune hiệu dụng tăng dần (tới ~4× + 60 ms) để các
//     dao động tự nhiên trong nốt ngân lọt qua.
//  6. Kẹp |lượng chỉnh| ≤ maxCorrection để lỡ dò sai cũng không làm giọng nhảy xa.
// Khoảng lặng ngắn (< 60 ms, vd. phụ âm) giữ nguyên trạng thái; dài hơn thì bắt đầu lại.
class PitchCorrector {
 public:
  explicit PitchCorrector(double frameRateHz);

  void setParams(const CorrectorParams& p) noexcept { params_ = p; }
  const CorrectorParams& params() const noexcept { return params_; }
  void setKeyMask(uint16_t mask) noexcept { quantizer_.setMask(mask); }
  uint16_t keyMask() const noexcept { return quantizer_.mask(); }

  void reset() noexcept;

  // [RT]
  CorrectionFrame update(const TrackedPitch& p) noexcept;

 private:
  double dt_;
  CorrectorParams params_;
  NoteQuantizer quantizer_;
  Biquad slow_;
  bool started_ = false;
  int gapFrames_ = 0;
  int lastTarget_ = -1;
  double held_ = 0.0;
  float y1_ = 0.0f;  // 2 tầng làm mượt
  float y2_ = 0.0f;
};

}  // namespace tonecore
