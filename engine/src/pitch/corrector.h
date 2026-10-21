#pragma once

#include <cstdint>

#include "pitch/f0_tracker.h"
#include "pitch/scales.h"

namespace tonecore {

struct CorrectorParams {
  float retuneMs = 15.0f;         // thời gian kéo về nốt đích (0 = tức thì, tối đa 200)
  float humanize = 0.5f;          // 0..1 — nốt ngân càng lâu thì chỉnh càng chậm
  float flexCents = 0.0f;         // lệch dưới ngưỡng này thì để nguyên (không chỉnh)
  float vibratoPreserve = 0.5f;   // 0..1 — phần rung giọng / luyến trong nốt được giữ lại
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
//  1. Chia giọng hát thành từng NỐT: nốt mới bắt đầu khi cao độ rời trung bình nốt hiện tại quá
//     (0,7 + 1,3·p) nửa cung trong ≥ 2 khung — luôn lớn hơn biên độ rung giọng thường gặp; với
//     p lớn (Tự nhiên) luyến / dấu thanh trong âm tiết vẫn tính là một nốt.
//  2. Tâm nốt μ = trung bình cao độ trong nốt (cửa sổ trượt ≤ 400 ms ≈ 2 chu kỳ rung giọng),
//     bỏ 48 ms đầu nốt (đoạn "vuốt" vào nốt). Trung bình không trễ pha như bộ lọc thông thấp và
//     tự triệt tiêu rung giọng.
//  3. Tâm dùng để chỉnh:  c = p·μ + (1 − p)·m   (m: cao độ tức thời, p = vibratoPreserve).
//     p = 1 → dịch cả nốt cho tâm về đúng nốt, giữ nguyên rung giọng / luyến / dấu thanh trong
//     nốt; p = 0 → ép từng khung về nốt (kiểu robot). Trong 48 ms đầu nốt chưa có μ: phần p
//     giữ nguyên lượng chỉnh đang áp (đoạn vuốt / chuyển nốt đi qua nguyên dạng).
//  4. Nốt đích T = lượng tử c theo thang âm, có hysteresis. Lượng chỉnh mong muốn d = T − c,
//     bỏ qua nếu |d| < flex.
//  5. Làm mượt d bằng 2 tầng một cực nối tiếp (bậc 2), τ = retune/4 → đạt ~90% sau `retune`.
//     humanize: nốt giữ lâu (> 120 ms) thì retune hiệu dụng tăng dần (tới ~4× + 60 ms).
//  6. Kẹp |lượng chỉnh| ≤ maxCorrection để lỡ dò sai cũng không làm giọng nhảy xa.
// Khoảng lặng ngắn (< 60 ms, vd. phụ âm) giữ nguyên mọi trạng thái; dài hơn thì bắt đầu lại.
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
  static constexpr int kWindow = 100;  // 400 ms ở hop 4 ms

  void startNote() noexcept;

  double dt_;
  int settleFrames_;
  CorrectorParams params_;
  NoteQuantizer quantizer_;
  bool started_ = false;
  int gapFrames_ = 0;
  int lastTarget_ = -1;
  double held_ = 0.0;
  float y1_ = 0.0f;  // 2 tầng làm mượt
  float y2_ = 0.0f;

  // Nốt hiện tại.
  int noteFrames_ = 0;  // số khung kể từ đầu nốt
  float hist_[kWindow] = {};
  int histHead_ = 0;
  int histCount_ = 0;
  double histSum_ = 0.0;
  int farFrames_ = 0;
};

}  // namespace tonecore
