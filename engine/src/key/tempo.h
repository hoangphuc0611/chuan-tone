#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "key/analysis_common.h"

namespace tonecore {

// Dò tempo (BPM) toàn bài từ phần bộ gõ (HPSS).
//
//  1. Đường onset (J. P. Bello et al., "A Tutorial on Onset Detection in Music Signals", IEEE TSAP
//     2005): STFT ngắn (~43 ms, bước ~5 ms), nén log c = log(1 + γ·|X|), spectral flux
//     f_j = Σ_k max(0, c_j[k] − c_{j−1}[k]); trừ trung bình trượt ~0,5 s rồi cắt phần âm.
//  2. Tự tương quan r(τ) của đường onset (đã trừ trung bình). Răng lược bội chu kỳ:
//       S(τ) = Σ_{m=1..M} r(m·τ) / m
//     — nhịp thật có đỉnh ở τ, 2τ, 3τ…; nhịp gấp đôi (τ/2) không có tiếng gõ ở giữa phách nên thấp.
//     (Ý tưởng của G. Percival, G. Tzanetakis, "Streamlined Tempo Estimation Based on
//     Autocorrelation and Cross-correlation With Pulses", IEEE/ACM TASLP 2014.)
//  3. Nhân prior log-Gauss quanh priorBpm (D. Ellis, "Beat Tracking by Dynamic Programming", JNMR
//     2007: 120 BPM, σ = 1,4 quãng 8) để chọn quãng 8 tempo (bolero ~65 hay ~130?), lấy đỉnh trong
//     [minBpm, maxBpm], nội suy parabol để có BPM lẻ.
//  4. Độ tin cậy = r(τ*) / r(0): mức tuần hoàn của đường onset ở chu kỳ đã chọn.
struct TempoConfig {
  std::size_t fftSize = 512;
  std::size_t hop = 64;
  float compression = 100.0f;  // γ, trên biên độ đã chuẩn hoá (sin biên độ A → A)
  float meanWindowSec = 0.5f;
  float minBpm = 40.0f;
  float maxBpm = 240.0f;
  float priorBpm = 120.0f;
  float priorOctaves = 1.4f;
  int combHarmonics = 4;
  float minDurationSec = 5.0f;
};

struct TempoEstimate {
  float bpm = 0.0f;
  float confidence = 0.0f;
};

// Đường onset, mỗi giá trị cách nhau hop / rate giây.
std::vector<float> onsetEnvelope(const std::vector<float>& x, double rate, const TempoConfig& cfg);

// Trả TC_OK (out rỗng nếu bài quá ngắn / không có tiếng gõ), kResultCancelled hoặc
// TC_ERR_INVALID_ARG.
tc_result estimateTempo(const std::vector<float>& x, double rate, const TempoConfig& cfg,
                        const ProgressFn& onProgress, std::optional<TempoEstimate>& out);

}  // namespace tonecore
