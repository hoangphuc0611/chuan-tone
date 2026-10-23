#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "key/analysis_common.h"

namespace tonecore {

// Tách phần hoà âm (harmonic: nốt ngân, hợp âm) và phần bộ gõ (percussive: trống, tiếng gõ)
// bằng lọc trung vị trên phổ biên độ — D. Fitzgerald, "Harmonic/Percussive Separation using
// Median Filtering", DAFx 2010.
//
// Ý tưởng: trên spectrogram, âm hoà âm là các đường NGANG (một tần số kéo dài theo thời gian),
// tiếng gõ là các đường DỌC (một khoảnh khắc trải mọi tần số).
//   H = trung vị theo thời gian của |X| (giữ đường ngang, xoá đường dọc)
//   P = trung vị theo tần số của |X|   (giữ đường dọc, xoá đường ngang)
// Mặt nạ mềm (kiểu Wiener, luỹ thừa p): Mh = H^p / (H^p + P^p), Mp = 1 − Mh.
// Phổ hoà âm = Mh·X, phổ bộ gõ = Mp·X → ISTFT (overlap-add có cửa sổ, Allen & Rabiner 1977).
// Vì Mh + Mp = 1 nên harmonic + percussive = tín hiệu gốc (trừ sai số làm tròn).
//
// Chạy cuốn chiếu: chỉ giữ `timeMedian` khung phổ gần nhất trong bộ nhớ (trễ (timeMedian−1)/2
// khung), không giữ cả spectrogram. Ngoài audio thread (có cấp phát).
struct HpssConfig {
  std::size_t fftSize = 1024;  // ~85–93 ms ở 11–12 kHz
  std::size_t hop = 256;       // chồng 75%: Hann² cộng dồn bằng hằng số → tổng hợp lại chính xác
  int timeMedian = 17;         // số khung (lẻ), ~0,37 s
  int freqMedian = 17;         // số bin (lẻ), ~200 Hz
  float maskPower = 2.0f;
};

struct HpssOutput {
  std::vector<float> harmonic;
  std::vector<float> percussive;
};

// Gọi cho mỗi khung sau khi tách: biên độ phổ phần hoà âm (fftSize/2 + 1 bin) và thời điểm giữa
// khung (giây). Dùng để ước lượng tuning mà không cần tính STFT lần nữa.
using HarmonicFrameFn =
    std::function<void(const float* magnitude, std::size_t bins, double timeSec)>;

// Trả về TC_OK, kResultCancelled hoặc TC_ERR_INVALID_ARG (cấu hình sai).
tc_result separateHarmonicPercussive(const std::vector<float>& x, double rate,
                                     const HpssConfig& cfg, const HarmonicFrameFn& onHarmonicFrame,
                                     const ProgressFn& onProgress, HpssOutput& out);

}  // namespace tonecore
