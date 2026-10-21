#include "pitch/mpm.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

std::size_t nextPow2(std::size_t n) {
  std::size_t p = 4;
  while (p < n) p <<= 1;
  return p;
}

// Nội suy parabol qua 3 điểm (τ−1, a), (τ, b), (τ+1, c) → (độ lệch δ ∈ [−0,5; 0,5], đỉnh).
inline void parabolic(float a, float b, float c, float& delta, float& peak) noexcept {
  const float den = a - 2.0f * b + c;
  if (std::fabs(den) < 1e-12f) {
    delta = 0.0f;
    peak = b;
    return;
  }
  delta = std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f);
  peak = b - 0.25f * (a - c) * delta;
}

}  // namespace

Mpm::Mpm(const PitchDetectorConfig& cfg, float k)
    : cfg_(cfg),
      k_(k),
      minLag_(static_cast<std::size_t>(std::floor(cfg.sampleRate / cfg.maxF0))),
      maxLag_(std::min(cfg.window - 2,
                       static_cast<std::size_t>(std::ceil(cfg.sampleRate / cfg.minF0)))),
      fft_(nextPow2(cfg.window + maxLag_ + 1)),
      padded_(fft_.size(), 0.0f),
      spec_(fft_.bins()),
      acf_(fft_.size()),
      nsdf_(maxLag_ + 2, 0.0f) {}

// [RT]
PitchEstimate Mpm::analyze(const float* frame) noexcept {
  const std::size_t w = cfg_.window;
  std::copy(frame, frame + w, padded_.begin());
  std::fill(padded_.begin() + static_cast<std::ptrdiff_t>(w), padded_.end(), 0.0f);

  // r(τ) = IFFT(|X|²)
  fft_.forward(padded_.data(), spec_.data());
  for (auto& v : spec_) v = {std::norm(v), 0.0f};
  fft_.inverse(spec_.data(), acf_.data());

  // m(τ) và NSDF cho τ = 0..maxLag+1.
  double m = 0.0;
  for (std::size_t j = 0; j < w; ++j) m += 2.0 * static_cast<double>(frame[j]) * static_cast<double>(frame[j]);
  const std::size_t last = std::min(maxLag_ + 1, w - 1);
  for (std::size_t tau = 0; tau <= last; ++tau) {
    if (tau > 0) {
      const auto a = static_cast<double>(frame[tau - 1]);
      const auto b = static_cast<double>(frame[w - tau]);
      m -= a * a + b * b;
    }
    nsdf_[tau] = m > 1e-9 ? static_cast<float>(2.0 * static_cast<double>(acf_[tau]) / m) : 0.0f;
  }

  // Tìm các đỉnh khoá. Bỏ thuỳ dương đầu tiên (quanh τ = 0).
  std::size_t tau = 1;
  while (tau <= last && nsdf_[tau] > 0.0f) ++tau;

  // Lưu tối đa 64 đỉnh khoá trên stack (không cấp phát).
  constexpr std::size_t kMaxPeaks = 64;
  std::size_t peakTau[kMaxPeaks];
  std::size_t peaks = 0;
  float best = 0.0f;
  std::size_t curTau = 0;
  float curVal = 0.0f;
  bool inLobe = false;
  for (; tau <= last && peaks < kMaxPeaks; ++tau) {
    const float v = nsdf_[tau];
    if (v > 0.0f) {
      if (!inLobe) {
        inLobe = true;
        curVal = -1.0f;
      }
      if (v > curVal) {
        curVal = v;
        curTau = tau;
      }
    } else if (inLobe) {
      inLobe = false;
      if (curTau >= minLag_ && curTau < last) {
        peakTau[peaks++] = curTau;
        best = std::max(best, curVal);
      }
    }
  }
  if (inLobe && peaks < kMaxPeaks && curTau >= minLag_ && curTau < last) {
    peakTau[peaks++] = curTau;
    best = std::max(best, curVal);
  }
  if (peaks == 0 || best <= 0.0f) return {};

  const float threshold = k_ * best;
  for (std::size_t i = 0; i < peaks; ++i) {
    const std::size_t t = peakTau[i];
    if (nsdf_[t] < threshold) continue;
    float delta = 0.0f;
    float peak = nsdf_[t];
    parabolic(nsdf_[t - 1], nsdf_[t], nsdf_[t + 1], delta, peak);
    const double period = static_cast<double>(t) + static_cast<double>(delta);
    PitchEstimate e;
    e.f0 = static_cast<float>(cfg_.sampleRate / period);
    e.clarity = std::clamp(peak, 0.0f, 1.0f);
    const auto f0 = static_cast<double>(e.f0);
    if (f0 < cfg_.minF0 * 0.97 || f0 > cfg_.maxF0 * 1.03) return {};
    return e;
  }
  return {};
}

}  // namespace tonecore
