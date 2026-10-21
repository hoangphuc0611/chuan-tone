#pragma once

#include <cstddef>

#include "pitch/mpm.h"

namespace tonecore {

struct TrackerConfig {
  float silenceDb = -45.0f;    // RMS khung dưới mức này → vô thanh (chỉnh trên vocadito)
  float clarityOn = 0.80f;     // clarity tối thiểu để BẮT ĐẦU một đoạn hữu thanh
  float clarityOff = 0.75f;    // clarity tối thiểu để GIỮ đoạn hữu thanh (trễ hai ngưỡng)
  float onsetClarity = 0.90f;  // khung đầu đoạn dưới mức này phải được khung kế tiếp xác nhận
  float zcrMax = 0.30f;        // tỉ lệ qua 0 cao + clarity thấp → tiếng xát/gió
  float zcrClarity = 0.85f;
  float jumpCents = 700.0f;   // nhảy lớn hơn mức này cần xác nhận
  int jumpConfirmFrames = 3;  // số khung liên tiếp ổn định để chấp nhận bước nhảy lớn
  int medianFrames = 3;       // lọc trung vị (1, 3 hoặc 5); mỗi khung thêm trễ ~nửa hop
};

struct TrackedPitch {
  bool voiced = false;
  float f0 = 0.0f;          // Hz (0 khi vô thanh)
  float midi = 0.0f;        // cao độ dạng số MIDI thực (69 = La 440 Hz)
  float confidence = 0.0f;  // 0..1
};

// Theo dõi f0 theo thời gian từ các ước lượng từng khung.
//  - Hữu thanh / vô thanh: năng lượng + clarity (hai ngưỡng có trễ) + tỉ lệ qua 0 (ý tưởng
//    phân loại voicing của pYIN, Mauch & Dixon 2014, đơn giản hoá cho real-time).
//  - Chống lỗi quãng 8: khi đoạn hữu thanh đã ổn định (≥ 3 khung), bước nhảy > 700 cent chỉ
//    được chấp nhận khi giá trị mới giữ ổn định (lệch nhau < 100 cent, clarity ≥ clarityOn)
//    trong 3 khung liên tiếp;
//    trong lúc chờ giữ nguyên giá trị cũ. Đoạn mới bắt đầu (< 3 khung) thì không giữ, vì giá
//    trị đầu tiên chính là giá trị dễ sai nhất.
//  - Bắt đầu đoạn: khung đầu tiên chỉ được tin ngay nếu rất rõ (clarity ≥ 0,9); nếu không phải
//    chờ khung kế tiếp cho cùng nốt (±100 cent) → thêm tối đa 1 hop trễ khi bắt đầu hát.
//  - Lọc trung vị trên thang log (cent) để bỏ giá trị lẻ loi.
// Mọi trạng thái là mảng cố định → dùng được trong audio thread.
class F0Tracker {
 public:
  explicit F0Tracker(const TrackerConfig& cfg = {}) : cfg_(cfg) {}

  void reset() noexcept;

  // [RT] Một khung: ước lượng thô + RMS (dBFS) + tỉ lệ qua 0 (lần/mẫu).
  TrackedPitch update(const PitchEstimate& est, float rmsDb, float zcr) noexcept;

  const TrackerConfig& config() const noexcept { return cfg_; }

 private:
  TrackerConfig cfg_;
  bool voiced_ = false;
  int run_ = 0;  // số khung hữu thanh liên tiếp
  bool onsetPending_ = false;
  float onsetMidi_ = 0.0f;
  float lastMidi_ = 0.0f;  // giá trị đã chấp nhận gần nhất (trước median)
  int pendingCount_ = 0;
  float pendingMidi_ = 0.0f;
  float hist_[5] = {};
  int histCount_ = 0;
};

}  // namespace tonecore
