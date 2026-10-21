#pragma once

#include <cstdint>

namespace tonecore {

// Thang âm (giá trị khớp với tc_scale trong tonecore.h).
enum class Scale : int32_t {
  Major = 0,            // trưởng:          0 2 4 5 7 9 11
  NaturalMinor = 1,     // thứ tự nhiên:     0 2 3 5 7 8 10
  HarmonicMinor = 2,    // thứ hoà âm:       0 2 3 5 7 8 11
  PentatonicMajor = 3,  // ngũ cung trưởng:  0 2 4 7 9
  PentatonicMinor = 4,  // ngũ cung thứ:     0 3 5 7 10
  Chromatic = 5,        // 12 nốt
};

inline constexpr int kScaleCount = 6;

// Mặt nạ 12 bit các bậc của thang âm tính từ chủ âm (bit i = nửa cung thứ i).
uint16_t scaleIntervals(Scale s) noexcept;

// Mặt nạ 12 bit theo cao độ tuyệt đối (bit 0 = Đô/C) cho chủ âm `root` (0 = Đô … 11 = Si).
uint16_t keyMask(int root, Scale s) noexcept;

// Nốt (số MIDI nguyên) thuộc `mask` gần `midi` nhất. Hai nốt cách đều thì chọn nốt thấp hơn.
int nearestNoteInMask(float midi, uint16_t mask) noexcept;

// Lượng tử cao độ về nốt trong thang, có vùng trễ (hysteresis): chỉ chuyển sang nốt mới khi cao
// độ vượt qua ranh giới giữa hai nốt thêm `hysteresisCents` → người hát lơ lửng giữa hai nốt
// không làm đầu ra nhảy qua lại.
class NoteQuantizer {
 public:
  void setMask(uint16_t mask) noexcept { mask_ = mask == 0 ? 0x0FFFu : mask; }
  uint16_t mask() const noexcept { return mask_; }
  void reset() noexcept { current_ = -1; }

  // [RT]
  int quantize(float midi, float hysteresisCents) noexcept;

 private:
  uint16_t mask_ = 0x0FFFu;
  int current_ = -1;
};

}  // namespace tonecore
