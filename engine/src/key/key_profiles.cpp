#include "key/key_profiles.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace tonecore {

// Số liệu theo các công bố gốc (nguồn ghi ở từng profile và trong docs/references.md).
// Thứ tự bậc: 1, ♭2, 2, ♭3, 3, 4, ♯4, 5, ♭6, 6, ♭7, 7.
// Đối chiếu 2026-10-23: "kk" và "temperley-kp" khớp từng số với music21 (BSD-3,
// analysis/discrete.py, trích từ tài liệu Humdrum keycor). "temperley" (1999) CHƯA đối chiếu được
// bản gốc — chỉ dùng để so sánh.
//
// Chưa đưa EDMA (Faraldo 2016) và Albrecht–Shanahan (2013) vào: cần số liệu từ bản gốc của bài
// báo. Thay vào đó có "vn-karaoke" — học từ chính nhạc Việt (M3 bước B4).
const std::vector<KeyProfile>& builtinKeyProfiles() {
  static const std::vector<KeyProfile> profiles = {
      // Học từ 50 beat karaoke Việt (tập dev, nhãn = tone ghi trên tiêu đề video), 2026-10-26, bằng
      // tools/keytest/train_profile.py (tools/keytest/profiles/vn-karaoke.txt). Kiểm chéo 5 phần
      // trên dev: 0,790 so với temperley-kp 0,758. Trên GuitarSet (jazz/funk) thấp hơn: 0,642.
      {"vn-karaoke",
       "Học từ 50 beat karaoke Việt (tập dev ChuẩnTone, 2026-10-26)",
       {0.150654f, 0.034875f, 0.110364f, 0.039581f, 0.126319f, 0.069869f, 0.046029f, 0.167100f,
        0.035302f, 0.094098f, 0.040072f, 0.085737f},
       {0.162951f, 0.032839f, 0.104714f, 0.105948f, 0.033762f, 0.101787f, 0.035445f, 0.171128f,
        0.064996f, 0.042076f, 0.096034f, 0.048321f},
       std::array<float, 12>{0.155957f, 0.073033f, 0.105043f, 0.069516f, 0.089496f, 0.080845f,
                             0.063221f, 0.128256f, 0.047019f, 0.071636f, 0.038151f, 0.077827f},
       std::array<float, 12>{0.145924f, 0.055681f, 0.075088f, 0.081170f, 0.049721f, 0.091291f,
                             0.060600f, 0.132994f, 0.088104f, 0.057178f, 0.089636f, 0.072611f}},
      {"kk",
       "C. Krumhansl, E. Kessler, Psychological Review 1982 (probe-tone ratings)",
       {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f},
       {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f},
       std::nullopt,
       std::nullopt},
      {"temperley",
       "D. Temperley, \"What's key for key?\", Music Perception 1999 (profile đã chỉnh; chưa đối "
       "chiếu)",
       {5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f},
       {5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f},
       std::nullopt,
       std::nullopt},
      {"temperley-kp",
       "D. Temperley, Music and Probability, MIT Press 2007 (kho Kostka–Payne)",
       {0.748f, 0.060f, 0.488f, 0.082f, 0.670f, 0.460f, 0.096f, 0.715f, 0.104f, 0.366f, 0.057f,
        0.400f},
       {0.712f, 0.084f, 0.474f, 0.618f, 0.049f, 0.460f, 0.105f, 0.747f, 0.404f, 0.067f, 0.133f,
        0.330f},
       std::nullopt,
       std::nullopt},
      // Mốc so sánh: chỉ đánh dấu nốt thuộc thang (thứ: thứ tự nhiên + bậc 7 nâng của thứ hoà âm,
      // mỗi dạng bậc 7 một nửa).
      {"diatonic",
       "thang âm nhị phân (mốc so sánh)",
       {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
       {1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.5f},
       std::nullopt,
       std::nullopt},
  };
  return profiles;
}

const KeyProfile* findKeyProfile(const std::string& name) {
  for (const KeyProfile& p : builtinKeyProfiles()) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

bool loadKeyProfileFile(const std::string& path, KeyProfile& out) {
  std::ifstream in(path);
  if (!in) return false;
  std::vector<std::array<float, 12>> rows;
  for (std::string line; std::getline(in, line);) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') continue;
    for (char& c : line) {
      if (c == ',') c = ' ';
    }
    std::istringstream ss(line);
    std::array<float, 12> row{};
    std::size_t n = 0;
    for (float v; n < 12 && ss >> v; ++n) {
      if (!std::isfinite(v)) return false;
      row[n] = v;
    }
    std::string rest;
    if (n != 12 || (ss >> rest)) return false;
    rows.push_back(row);
  }
  if (rows.size() != 2 && rows.size() != 4) return false;
  out = KeyProfile{};
  out.name = "file";
  out.source = path;
  out.major = rows[0];
  out.minor = rows[1];
  if (rows.size() == 4) {
    out.bassMajor = rows[2];
    out.bassMinor = rows[3];
  }
  return true;
}

}  // namespace tonecore
