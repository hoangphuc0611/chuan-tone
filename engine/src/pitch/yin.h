#pragma once

#include <cstddef>
#include <vector>

#include "pitch/mpm.h"

namespace tonecore {

// YIN (A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and
// music", JASA 2002) — dùng làm đối chứng với MPM trong test / đánh giá, không chạy real-time.
//
//  1. Hàm hiệu  d(τ) = Σ_{j<Wi} (x[j] − x[j+τ])²,   Wi = W − τmax (cửa sổ tích phân cố định),
//     đoạn tích phân đặt đối xứng quanh tâm cửa sổ.
//  2. Chuẩn hoá theo trung bình tích luỹ (CMNDF):  d'(τ) = d(τ) · τ / Σ_{1..τ} d.
//  3. Ngưỡng tuyệt đối: τ nhỏ nhất có d' < 0,1 rồi đi xuống tới cực tiểu cục bộ; không có thì
//     lấy cực tiểu toàn cục.
//  4. Nội suy parabol. Độ tin cậy = 1 − d'(τ).
class Yin {
 public:
  explicit Yin(const PitchDetectorConfig& cfg, float threshold = 0.1f);

  std::size_t window() const noexcept { return cfg_.window; }
  PitchEstimate analyze(const float* frame) noexcept;

 private:
  PitchDetectorConfig cfg_;
  float threshold_;
  std::size_t minLag_;
  std::size_t maxLag_;
  std::vector<float> d_;
};

}  // namespace tonecore
