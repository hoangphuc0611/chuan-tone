#include "pitch/scales.h"

#include <cmath>
#include <initializer_list>

namespace tonecore {

namespace {

constexpr uint16_t bits(std::initializer_list<int> steps) {
  uint16_t m = 0;
  for (int s : steps) m = static_cast<uint16_t>(m | (1u << s));
  return m;
}

inline bool inMask(int note, uint16_t mask) noexcept {
  const int pc = ((note % 12) + 12) % 12;
  return (mask >> pc) & 1u;
}

}  // namespace

uint16_t scaleIntervals(Scale s) noexcept {
  switch (s) {
    case Scale::Major:
      return bits({0, 2, 4, 5, 7, 9, 11});
    case Scale::NaturalMinor:
      return bits({0, 2, 3, 5, 7, 8, 10});
    case Scale::HarmonicMinor:
      return bits({0, 2, 3, 5, 7, 8, 11});
    case Scale::PentatonicMajor:
      return bits({0, 2, 4, 7, 9});
    case Scale::PentatonicMinor:
      return bits({0, 3, 5, 7, 10});
    case Scale::Chromatic:
    default:
      return 0x0FFF;
  }
}

uint16_t keyMask(int root, Scale s) noexcept {
  const uint32_t iv = scaleIntervals(s);
  const int r = ((root % 12) + 12) % 12;
  // Xoay vòng 12 bit sang trái r bậc.
  const uint32_t rotated = ((iv << r) | (iv >> (12 - r))) & 0x0FFFu;
  return static_cast<uint16_t>(rotated);
}

// [RT]
int nearestNoteInMask(float midi, uint16_t mask) noexcept {
  if (mask == 0) mask = 0x0FFF;
  const int base = static_cast<int>(std::floor(midi));
  int best = base;
  float bestDist = 1e9f;
  // Thang thưa nhất (ngũ cung) có khoảng cách tối đa 3 nửa cung → tìm ±3 là đủ.
  for (int n = base - 3; n <= base + 4; ++n) {
    if (!inMask(n, mask)) continue;
    const float d = std::fabs(midi - static_cast<float>(n));
    if (d < bestDist - 1e-6f) {
      bestDist = d;
      best = n;
    }
  }
  return best;
}

// [RT]
int NoteQuantizer::quantize(float midi, float hysteresisCents) noexcept {
  const int cand = nearestNoteInMask(midi, mask_);
  if (current_ < 0 || !inMask(current_, mask_) || cand == current_) {
    current_ = cand;
    return current_;
  }
  // Ranh giới giữa nốt cũ và nốt mới nằm ở giữa; đòi vượt quá thêm `hysteresis`:
  // |midi − cũ| − |midi − mới| > 2·h.
  const float dOld = std::fabs(midi - static_cast<float>(current_));
  const float dNew = std::fabs(midi - static_cast<float>(cand));
  if (dOld - dNew > 2.0f * hysteresisCents / 100.0f) current_ = cand;
  return current_;
}

}  // namespace tonecore
