#include "key/tuning.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

constexpr double kSigmaCents = 4.0;
constexpr int kSmoothReach = 12;  // ±3σ
constexpr double kPeakRangeDb = 30.0;
constexpr double kFrameRangeDb = 40.0;
constexpr std::size_t kMinPeaks = 20;

int wrap(int i, int n) {
  return ((i % n) + n) % n;
}

}  // namespace

TuningEstimator::TuningEstimator(double rate, std::size_t fftSize, double minHz, double maxHz)
    : rate_(rate), fftSize_(fftSize) {
  const std::size_t bins = fftSize / 2 + 1;
  kMin_ = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::ceil(minHz * static_cast<double>(fftSize) / rate)));
  const double top = std::min(maxHz, 0.45 * rate);
  kMax_ = std::min(bins - 2,
                   static_cast<std::size_t>(std::floor(top * static_cast<double>(fftSize) / rate)));
}

void TuningEstimator::addFrame(const float* mag, std::size_t bins) {
  if (bins != fftSize_ / 2 + 1 || kMin_ >= kMax_) return;
  float peak = 0.0f;
  for (std::size_t k = kMin_; k <= kMax_; ++k) peak = std::max(peak, mag[k]);
  float framePeak = peak;
  for (std::size_t k = 0; k < bins; ++k) framePeak = std::max(framePeak, mag[k]);
  if (!(peak > 1e-9f)) return;  // khung im lặng
  // Đỉnh phải trong 30 dB so với đỉnh của dải VÀ trong 40 dB so với đỉnh của cả khung — nếu không,
  // dải 400–3000 Hz chỉ có nhiễu (vd. chỉ có nốt bass) cũng sinh "đỉnh" giả.
  const float thr = std::max(peak * static_cast<float>(std::pow(10.0, -kPeakRangeDb / 20.0)),
                             framePeak * static_cast<float>(std::pow(10.0, -kFrameRangeDb / 20.0)));
  for (std::size_t k = kMin_; k <= kMax_; ++k) {
    const float m = mag[k];
    if (m < thr || !(m > mag[k - 1]) || m < mag[k + 1]) continue;
    // Nội suy parabol trên log biên độ: đỉnh thật lệch δ bin so với bin k.
    const double a = std::log(static_cast<double>(mag[k - 1]) + 1e-20);
    const double b = std::log(static_cast<double>(m));
    const double c = std::log(static_cast<double>(mag[k + 1]) + 1e-20);
    const double denom = a - 2.0 * b + c;
    if (!(denom < 0.0)) continue;
    const double delta = 0.5 * (a - c) / denom;
    const double freq = (static_cast<double>(k) + delta) * rate_ / static_cast<double>(fftSize_);
    const double cents = 1200.0 * std::log2(freq / 440.0);
    const double dev = cents - 100.0 * std::round(cents / 100.0);  // [−50, 50]
    // Chia trọng số tuyến tính cho 2 ô kề nhau → độ phân giải dưới 1 cent.
    const double pos = dev + 50.0;
    const double base = std::floor(pos);
    const double frac = pos - base;
    const int i0 = wrap(static_cast<int>(base), kBins);
    const auto weight = static_cast<double>(m);
    hist_[static_cast<std::size_t>(i0)] += weight * (1.0 - frac);
    hist_[static_cast<std::size_t>(wrap(i0 + 1, kBins))] += weight * frac;
    ++peaks_;
  }
}

std::optional<float> TuningEstimator::estimate() const {
  if (peaks_ < kMinPeaks) return std::nullopt;
  std::array<double, kBins> smooth{};
  for (int i = 0; i < kBins; ++i) {
    double acc = 0.0;
    for (int d = -kSmoothReach; d <= kSmoothReach; ++d) {
      const double g = std::exp(-0.5 * d * d / (kSigmaCents * kSigmaCents));
      acc += g * hist_[static_cast<std::size_t>(wrap(i + d, kBins))];
    }
    smooth[static_cast<std::size_t>(i)] = acc;
  }
  const auto best =
      static_cast<int>(std::max_element(smooth.begin(), smooth.end()) - smooth.begin());
  if (!(smooth[static_cast<std::size_t>(best)] > 0.0)) return std::nullopt;
  const double ym = smooth[static_cast<std::size_t>(wrap(best - 1, kBins))];
  const double y0 = smooth[static_cast<std::size_t>(best)];
  const double yp = smooth[static_cast<std::size_t>(wrap(best + 1, kBins))];
  const double denom = ym - 2.0 * y0 + yp;
  const double offset = denom < 0.0 ? 0.5 * (ym - yp) / denom : 0.0;
  double cents = best + offset - 50.0;
  if (cents >= 50.0) cents -= 100.0;
  if (cents < -50.0) cents += 100.0;
  return static_cast<float>(cents);
}

}  // namespace tonecore
