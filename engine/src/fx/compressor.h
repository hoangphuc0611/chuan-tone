#pragma once

#include <cstdint>

namespace tonecore {

struct CompressorParams {
  float thresholdDb = -18.0f;  // dBFS (mức đỉnh tương đương của sin: sin toàn thang = 0 dB)
  float ratio = 3.0f;          // ≥ 1
  float kneeDb = 6.0f;         // độ rộng knee mềm (0 = gãy)
  float attackMs = 5.0f;
  float releaseMs = 80.0f;
  float makeupDb = 0.0f;
  float rmsMs = 10.0f;  // cửa sổ RMS của bộ dò mức
};

// Compressor giọng (CLAUDE.md 3.7), feed-forward, mono, tại chỗ, không trễ — theo D. Giannoulis,
// M. Massberg, J. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
// JAES 2012 (thiết kế "log domain, smooth branching"):
//  1. Mức vào: RMS một cực (τ = rmsMs) → x_G = 10·log10(2·trung bình x²) dB (sin toàn thang = 0
//  dB).
//  2. Đường nén tĩnh (knee mềm W, ngưỡng T, tỉ lệ R):
//       y_G = x_G                                   khi 2(x_G − T) < −W
//       y_G = x_G + (1/R − 1)·(x_G − T + W/2)²/(2W) khi |2(x_G − T)| ≤ W
//       y_G = T + (x_G − T)/R                       khi 2(x_G − T) > W
//     → mức giảm g_c = y_G − x_G ≤ 0.
//  3. Làm mượt trong miền dB, tách nhánh: giảm thêm → attack, nhả bớt → release:
//       g_s ← α·g_s + (1 − α)·g_c,  α = e^(−1/(τ·fs)).
//  4. y = x · 10^((g_s + makeup)/20). Đổi ngưỡng / tỉ lệ khi đang chạy thì g_c nhảy nhưng g_s vẫn
//     trượt theo attack / release → không click; makeup trượt riêng (τ 20 ms).
// Dưới ngưỡng (g_s = 0, makeup 0) hệ số đúng bằng 1 → đi thẳng từng mẫu. Cấp phát: không.
class Compressor {
 public:
  explicit Compressor(double sampleRate);

  void setParams(const CompressorParams& p) noexcept;  // [RT]
  void reset() noexcept;
  void process(float* x, uint32_t n) noexcept;  // [RT]

  // Mức giảm hiện tại (dB, ≤ 0) — cho đồng hồ giảm mức / test.
  float gainReductionDb() const noexcept { return gs_; }
  // Đường nén tĩnh g_c (dB, ≤ 0) tại mức vào inDb.
  static double staticGainDb(double inDb, const CompressorParams& p) noexcept;

 private:
  double fs_;
  CompressorParams p_;
  float rmsCoeff_ = 1.0f;
  float attack_ = 0.0f;   // α_A
  float release_ = 0.0f;  // α_R
  float makeupCoeff_ = 1.0f;
  float makeupTarget_ = 0.0f;
  float makeup_ = 0.0f;
  float ms_ = 0.0f;  // trung bình x²
  float gs_ = 0.0f;  // dB
};

}  // namespace tonecore
