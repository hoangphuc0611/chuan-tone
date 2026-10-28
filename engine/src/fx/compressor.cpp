#include "fx/compressor.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr float kDbToLog = 0.11512925464970229f;  // ln(10)/20: 10^(dB/20) = e^(dB·ln10/20)
constexpr double kMakeupSec = 0.020;

float decay(double timeSec, double fs) {
  return timeSec <= 0.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / (timeSec * fs)));
}
}  // namespace

Compressor::Compressor(double sampleRate) : fs_(sampleRate) {
  makeupCoeff_ = 1.0f - decay(kMakeupSec, sampleRate);
  setParams(CompressorParams{});
  makeup_ = makeupTarget_;
}

void Compressor::setParams(const CompressorParams& p) noexcept {
  p_ = p;
  p_.ratio = std::max(1.0f, p.ratio);
  p_.kneeDb = std::max(0.0f, p.kneeDb);
  rmsCoeff_ = 1.0f - decay(1e-3 * static_cast<double>(p.rmsMs), fs_);
  attack_ = decay(1e-3 * static_cast<double>(p.attackMs), fs_);
  release_ = decay(1e-3 * static_cast<double>(p.releaseMs), fs_);
  makeupTarget_ = p.makeupDb;
}

void Compressor::reset() noexcept {
  ms_ = 0.0f;
  gs_ = 0.0f;
  makeup_ = makeupTarget_;
}

double Compressor::staticGainDb(double inDb, const CompressorParams& p) noexcept {
  const double t = static_cast<double>(p.thresholdDb);
  const double r = std::max(1.0, static_cast<double>(p.ratio));
  const double w = std::max(0.0, static_cast<double>(p.kneeDb));
  const double over = inDb - t;
  if (w <= 0.0) return over > 0.0 ? (1.0 / r - 1.0) * over : 0.0;
  if (2.0 * over < -w) return 0.0;
  if (2.0 * over > w) return (t + over / r) - inDb;
  const double d = over + w / 2.0;
  return (1.0 / r - 1.0) * d * d / (2.0 * w);
}

// [RT]
void Compressor::process(float* x, uint32_t n) noexcept {
  const float t = p_.thresholdDb;
  const float w = p_.kneeDb;
  const float slope = 1.0f / p_.ratio - 1.0f;  // ≤ 0
  for (uint32_t i = 0; i < n; ++i) {
    const float v = x[i];
    ms_ += rmsCoeff_ * (v * v - ms_);
    const float level = 10.0f * std::log10(2.0f * ms_ + 1e-12f);
    const float over = level - t;
    float gc = 0.0f;
    if (2.0f * over > w) {
      gc = slope * over;
    } else if (2.0f * over >= -w && w > 0.0f) {
      const float d = over + 0.5f * w;
      gc = slope * d * d / (2.0f * w);
    }
    gs_ =
        gc < gs_ ? attack_ * gs_ + (1.0f - attack_) * gc : release_ * gs_ + (1.0f - release_) * gc;
    makeup_ += makeupCoeff_ * (makeupTarget_ - makeup_);
    x[i] = v * std::exp(kDbToLog * (gs_ + makeup_));
  }
}

}  // namespace tonecore
