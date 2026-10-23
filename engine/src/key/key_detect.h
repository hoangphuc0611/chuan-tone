#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "key/chroma.h"
#include "key/key_profiles.h"
#include "key/musical_key.h"

namespace tonecore {

// Dò tone toàn bài từ chroma (Krumhansl–Schmuckler mở rộng có dải bass):
//  1. Bỏ khung im lặng (thấp hơn mức phân vị 95 của bài quá `silenceDb`, hoặc dưới −120 dB).
//  2. Cộng dồn chroma bass và treble các khung còn lại (tuỳ chọn chuẩn hoá từng khung trước).
//  3. Với mỗi tone (24 tone: 12 chủ âm × trưởng/thứ): xoay profile tới chủ âm đó, tính hệ số
//     tương quan Pearson r với chroma treble và với chroma bass;
//       điểm = (1 − w)·r_treble + w·r_bass,   w = bassWeight.
//  4. Tone điểm cao nhất thắng. Độ tin cậy = (mức áp đảo các tone khác) × (độ rõ tone):
//       p = exp(0) / Σ_j exp((s_j − s_max)/T)            softmax, nhiệt độ T
//       c = clamp((s_max − rMin) / (rFull − rMin), 0, 1)  tương quan thấp = không rõ tone
//     Cần cả hai: cụm đủ 12 nốt có s_max ≈ 0,3 nhưng chỉ 2 tone sát nhau → p ≈ 0,5 mà vẫn là
//     "không có tone". rMin = 0,3 / rFull = 0,7 là giá trị tạm, hiệu chỉnh trên tập dev ở B4.
enum class ChromaFrameNorm : int32_t {
  None = 0,  // khung to đóng góp nhiều hơn (chroma đã nén log)
  L1 = 1,    // mỗi khung tổng = 1: mọi khung có tiếng ngang nhau
  Max = 2    // mỗi khung chia cho giá trị lớn nhất
};

// Mặc định tạm (B3): tốt nhất khi quét 4 profile × bass {0; 0,25; 0,5} × chuẩn hoá {none, l1}
// trên GuitarSet (0,706 so với 0,608 của kk/none). Chọn lại trên tập dev nhạc Việt ở B4.
struct KeyDetectConfig {
  std::string profile = "temperley-kp";
  float bassWeight = 0.25f;
  ChromaFrameNorm frameNorm = ChromaFrameNorm::L1;
  float silenceDb = 40.0f;
  float minVoicedSec = 3.0f;  // ít hơn → không kết luận tone
  float temperature = 0.02f;
  float clarityMin = 0.3f;   // rMin
  float clarityFull = 0.7f;  // rFull
};

using KeyScores = std::array<float, 24>;  // chỉ số keyIndex(): trưởng 0..11, thứ 12..23

struct KeyEstimate {
  MusicalKey key{};
  float confidence = 0.0f;
  KeyScores scores{};
};

// Hệ số tương quan Pearson; 0 nếu một trong hai vector hằng.
float pearson(const ChromaVector& a, const std::array<float, 12>& b);

// Điểm 24 tone cho một cặp chroma đã cộng dồn.
KeyScores scoreKeys(const ChromaVector& bass, const ChromaVector& treble, const KeyProfile& profile,
                    float bassWeight);

// Khung nào có tiếng (1) / im lặng (0), ngưỡng tính trên cả bài.
std::vector<uint8_t> voicedFrames(const Chromagram& ch, float silenceDb);

// Cộng dồn chroma các khung có tiếng trong [from, to). Trả về số khung đã cộng.
std::size_t accumulateChroma(const Chromagram& ch, const std::vector<uint8_t>& voiced,
                             std::size_t from, std::size_t to, ChromaFrameNorm norm,
                             ChromaVector& bass, ChromaVector& treble);

// Tone + độ tin cậy từ điểm 24 tone.
KeyEstimate pickKey(const KeyScores& scores, const KeyDetectConfig& cfg);

// Rỗng nếu profile không tồn tại hoặc bài có ít hơn minVoicedSec giây có tiếng.
std::optional<KeyEstimate> estimateGlobalKey(const Chromagram& ch, const KeyDetectConfig& cfg);

}  // namespace tonecore
