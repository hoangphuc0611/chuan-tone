#include "key/key_profiles.h"

namespace tonecore {

// Số liệu theo các công bố gốc (nguồn ghi ở từng profile và trong docs/references.md).
// Thứ tự bậc: 1, ♭2, 2, ♭3, 3, 4, ♯4, 5, ♭6, 6, ♭7, 7.
// Đối chiếu 2026-10-23: "kk" và "temperley-kp" khớp từng số với music21 (BSD-3,
// analysis/discrete.py, trích từ tài liệu Humdrum keycor). "temperley" (1999) CHƯA đối chiếu được
// bản gốc — chỉ dùng để so sánh.
//
// Chưa đưa EDMA (Faraldo 2016) và Albrecht–Shanahan (2013) vào: cần số liệu từ bản gốc của bài
// báo. Ở B4 sẽ có thêm profile học từ tập dev nhạc Việt.
const std::vector<KeyProfile>& builtinKeyProfiles() {
  static const std::vector<KeyProfile> profiles = {
      {"kk",
       "C. Krumhansl, E. Kessler, Psychological Review 1982 (probe-tone ratings)",
       {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f},
       {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f}},
      {"temperley",
       "D. Temperley, \"What's key for key?\", Music Perception 1999 (profile đã chỉnh; chưa đối "
       "chiếu)",
       {5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f},
       {5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f}},
      {"temperley-kp",
       "D. Temperley, Music and Probability, MIT Press 2007 (kho Kostka–Payne)",
       {0.748f, 0.060f, 0.488f, 0.082f, 0.670f, 0.460f, 0.096f, 0.715f, 0.104f, 0.366f, 0.057f,
        0.400f},
       {0.712f, 0.084f, 0.474f, 0.618f, 0.049f, 0.460f, 0.105f, 0.747f, 0.404f, 0.067f, 0.133f,
        0.330f}},
      // Mốc so sánh: chỉ đánh dấu nốt thuộc thang (thứ: thứ tự nhiên + bậc 7 nâng của thứ hoà âm,
      // mỗi dạng bậc 7 một nửa).
      {"diatonic",
       "thang âm nhị phân (mốc so sánh)",
       {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f}},
  };
  return profiles;
}

const KeyProfile* findKeyProfile(const std::string& name) {
  for (const KeyProfile& p : builtinKeyProfiles()) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

}  // namespace tonecore
