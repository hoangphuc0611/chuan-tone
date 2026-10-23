#pragma once

#include <cstdint>

namespace tonecore {

enum class KeyMode : int32_t { Major = 0, Minor = 1 };

struct MusicalKey {
  int32_t root = 0;  // 0 = Đô (C) … 11 = Si (B)
  KeyMode mode = KeyMode::Major;

  bool operator==(const MusicalKey& o) const { return root == o.root && mode == o.mode; }
  bool operator!=(const MusicalKey& o) const { return !(*this == o); }
};

// Chỉ số 0..23 dùng cho mảng điểm 24 tone: trưởng 0..11, thứ 12..23.
inline int keyIndex(const MusicalKey& k) {
  return static_cast<int>(k.mode) * 12 + k.root;
}
inline MusicalKey keyFromIndex(int i) {
  return {i % 12, i >= 12 ? KeyMode::Minor : KeyMode::Major};
}

}  // namespace tonecore
