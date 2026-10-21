#include "pitch/yin.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

Yin::Yin(const PitchDetectorConfig& cfg, float threshold)
    : cfg_(cfg),
      threshold_(threshold),
      minLag_(std::max<std::size_t>(2, static_cast<std::size_t>(cfg.sampleRate / cfg.maxF0))),
      maxLag_(std::min(cfg.window / 2,
                       static_cast<std::size_t>(std::ceil(cfg.sampleRate / cfg.minF0)))),
      d_(maxLag_ + 2, 0.0f) {}

PitchEstimate Yin::analyze(const float* frame) noexcept {
  const std::size_t wi = cfg_.window - maxLag_ - 1;
  const std::size_t w = cfg_.window;
  // Bước 1–2: hàm hiệu + CMNDF.
  d_[0] = 1.0f;
  double running = 0.0;
  for (std::size_t tau = 1; tau <= maxLag_ + 1; ++tau) {
    // Đặt cặp (x[j], x[j+τ]) đối xứng quanh tâm cửa sổ để thời điểm ước lượng khớp với MPM.
    const std::size_t start = (w - wi - tau) / 2;
    double acc = 0.0;
    for (std::size_t j = start; j < start + wi; ++j) {
      const double diff = static_cast<double>(frame[j]) - static_cast<double>(frame[j + tau]);
      acc += diff * diff;
    }
    running += acc;
    d_[tau] = running > 1e-12 ? static_cast<float>(acc * static_cast<double>(tau) / running) : 1.0f;
  }

  // Bước 3: ngưỡng tuyệt đối.
  std::size_t best = 0;
  for (std::size_t tau = minLag_; tau <= maxLag_; ++tau) {
    if (d_[tau] < threshold_) {
      while (tau + 1 <= maxLag_ && d_[tau + 1] < d_[tau]) ++tau;
      best = tau;
      break;
    }
  }
  if (best == 0) {
    best = minLag_;
    for (std::size_t tau = minLag_; tau <= maxLag_; ++tau) {
      if (d_[tau] < d_[best]) best = tau;
    }
  }

  // Bước 4: nội suy parabol trên d'.
  const float a = d_[best - 1];
  const float b = d_[best];
  const float c = d_[best + 1];
  const float den = a - 2.0f * b + c;
  float delta = std::fabs(den) > 1e-12f ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
  const double period = static_cast<double>(best) + static_cast<double>(delta);
  PitchEstimate e;
  e.f0 = static_cast<float>(cfg_.sampleRate / period);
  e.clarity = std::clamp(1.0f - b, 0.0f, 1.0f);
  return e;
}

}  // namespace tonecore
