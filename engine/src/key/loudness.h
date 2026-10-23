#pragma once

#include <optional>

#include "decode/decoder.h"
#include "dsp/biquad.h"

namespace tonecore {

// Độ to tích hợp (integrated loudness, LUFS) theo ITU-R BS.1770-4 — dùng để chuẩn hoá âm lượng
// beat.
//
//  1. Lọc K-weighting từng kênh: kệ cao (+4 dB trên ~1,7 kHz, mô phỏng đầu người) rồi thông cao
//     RLB (~38 Hz). Chuẩn chỉ cho hệ số ở 48 kHz; tần số khác dùng nguyên mẫu analog (f0, Q, độ
//     lợi) suy ra từ chính bộ hệ số 48 kHz đó rồi biến đổi song tuyến (bilinear) — test kiểm tra
//     lại được đúng bảng hệ số của chuẩn ở 48 kHz.
//  2. Khối 400 ms chồng 75% (bước 100 ms): z = Σ_kênh G_i · trung bình bình phương.
//     L_khối = −0,691 + 10·log10(z).
//  3. Cổng tuyệt đối −70 LUFS; cổng tương đối: bỏ khối thấp hơn trung bình (các khối qua cổng tuyệt
//     đối) quá 10 LU. Độ to tích hợp = −0,691 + 10·log10(trung bình z các khối còn lại).
//
// Đo ĐÚNG tín hiệu engine phát: beat mono được phát ra cả hai tai nên tính như mono kép (cộng hai
// lần, +3 dB so với cách chuẩn tính một kênh mono); file > 2 kênh thì engine chỉ phát 2 kênh đầu
// (player/track.cpp) nên chỉ đo 2 kênh đầu. Không có kênh LFE/surround nên mọi G_i = 1.
//
// Kiểm bằng các ca test EBU Tech 3341 (tự sinh tín hiệu): sin 1 kHz stereo −23 dBFS → −23,0 LUFS …
struct LoudnessResult {
  std::optional<float> integratedLufs;  // rỗng nếu mọi khối dưới cổng tuyệt đối (im lặng)
  float samplePeakDb = -200.0f;         // đỉnh mẫu (dBFS) trên các kênh được đo
};

// Hệ số K-weighting ở tần số lấy mẫu bất kỳ.
BiquadCoeffs kWeightingShelf(double sampleRate);
BiquadCoeffs kWeightingHighpass(double sampleRate);

// Chạy trên luồng nền. `audio` không bị thay đổi.
LoudnessResult measureLoudness(const DecodedAudio& audio);

}  // namespace tonecore
