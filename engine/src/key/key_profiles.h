#pragma once

#include <array>
#include <string>
#include <vector>

namespace tonecore {

// Key profile: mức "thuộc về tone" của 12 bậc (chỉ số 0 = chủ âm, 1 = chủ âm + nửa cung, …) cho
// giọng trưởng và giọng thứ. Dò tone = xoay profile qua 12 chủ âm và so với chroma của bài
// (thuật toán Krumhansl–Schmuckler, C. Krumhansl, "Cognitive Foundations of Musical Pitch", 1990).
struct KeyProfile {
  std::string name;
  std::string source;
  std::array<float, 12> major;
  std::array<float, 12> minor;
};

// Các profile có sẵn: "kk", "temperley", "temperley-kp", "diatonic" (xem key_profiles.cpp).
const std::vector<KeyProfile>& builtinKeyProfiles();

// nullptr nếu không có profile tên này.
const KeyProfile* findKeyProfile(const std::string& name);

}  // namespace tonecore
