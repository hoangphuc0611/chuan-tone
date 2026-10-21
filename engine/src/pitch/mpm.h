#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/fft.h"

namespace tonecore {

// Kết quả dò cao độ của một khung.
struct PitchEstimate {
  float f0 = 0.0f;       // Hz, 0 nếu không tìm được chu kỳ
  float clarity = 0.0f;  // 0..1 — độ "tuần hoàn" của khung (dùng làm độ tin cậy)
};

struct PitchDetectorConfig {
  double sampleRate = 16000.0;
  std::size_t window = 640;  // mẫu (40 ms ở 16 kHz)
  double minF0 = 70.0;
  double maxF0 = 1100.0;
};

// McLeod Pitch Method (P. McLeod, G. Wyvill, "A Smarter Way to Find Pitch", ICMC 2005).
//
// Hàm vi sai bình phương chuẩn hoá (NSDF):
//     n(τ) = 2·r(τ) / m(τ),   r(τ) = Σ x[j]·x[j+τ],   m(τ) = Σ (x[j]² + x[j+τ]²)
// (tổng trên j = 0..W−1−τ). n(τ) ∈ [−1, 1]; = 1 khi tín hiệu lặp lại hoàn hảo sau τ mẫu.
//  - r(τ) tính nhanh qua FFT: r = IFFT(|FFT(x đệm 0)|²), kích thước FFT ≥ W + τmax để không bị
//    chồng vòng.
//  - m(τ) tính dần: m(τ) = m(τ−1) − x[τ−1]² − x[W−τ]².
// Chọn đỉnh: bỏ thuỳ đầu (quanh τ = 0); trong mỗi đoạn n > 0 lấy một "đỉnh khoá" (key maximum);
// lấy đỉnh khoá ĐẦU TIÊN có giá trị ≥ k·(đỉnh khoá lớn nhất), k = 0,9 → tránh chọn nhầm bội số
// chu kỳ (lỗi hạ quãng 8). Nội suy parabol quanh đỉnh để có τ lẻ và "clarity".
class Mpm {
 public:
  explicit Mpm(const PitchDetectorConfig& cfg, float k = 0.9f);

  std::size_t window() const noexcept { return cfg_.window; }

  // [RT] `frame`: đúng window() mẫu, cũ → mới.
  PitchEstimate analyze(const float* frame) noexcept;

  // NSDF của lần analyze() gần nhất, chỉ số 0..maxLag (cho test / vẽ).
  const std::vector<float>& nsdf() const noexcept { return nsdf_; }

 private:
  PitchDetectorConfig cfg_;
  float k_;
  std::size_t minLag_;
  std::size_t maxLag_;
  RealFft fft_;
  std::vector<float> padded_;
  std::vector<std::complex<float>> spec_;
  std::vector<float> acf_;
  std::vector<float> nsdf_;
};

}  // namespace tonecore
