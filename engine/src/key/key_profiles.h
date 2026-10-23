#pragma once

#include <array>
#include <optional>
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
  // Profile riêng cho dải bass (vd. học từ dữ liệu); rỗng → bass dùng chung major / minor.
  std::optional<std::array<float, 12>> bassMajor;
  std::optional<std::array<float, 12>> bassMinor;
};

// Các profile có sẵn: "kk", "temperley", "temperley-kp", "diatonic" (xem key_profiles.cpp).
const std::vector<KeyProfile>& builtinKeyProfiles();

// nullptr nếu không có profile tên này.
const KeyProfile* findKeyProfile(const std::string& name);

// Đọc profile từ file văn bản (tools/keytest/train_profile.py ghi ra): 2 hoặc 4 dòng số, mỗi dòng
// 12 giá trị cách nhau bằng dấu cách / dấu phẩy — major, minor[, bass_major, bass_minor]; dòng
// trống hoặc bắt đầu bằng '#' bị bỏ qua. Trả false nếu sai định dạng.
bool loadKeyProfileFile(const std::string& path, KeyProfile& out);

}  // namespace tonecore
