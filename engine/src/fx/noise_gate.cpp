#include "fx/noise_gate.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
float onePole(double timeSec, double fs) {
  return timeSec <= 0.0 ? 1.0f : static_cast<float>(1.0 - std::exp(-1.0 / (timeSec * fs)));
}
float dbToLin(double db) {
  return static_cast<float>(std::pow(10.0, db / 20.0));
}
}  // namespace

NoiseGate::NoiseGate(double sampleRate)
    : fs_(sampleRate), envDecay_(static_cast<float>(std::exp(-1.0 / (kDetectorSec * sampleRate)))) {
  setParams(GateParams{});
  reset();
}

void NoiseGate::setParams(const GateParams& p) noexcept {
  openLevel_ = dbToLin(static_cast<double>(p.thresholdDb));
  closeLevel_ = dbToLin(static_cast<double>(p.thresholdDb) - static_cast<double>(p.hysteresisDb));
  floor_ = dbToLin(static_cast<double>(std::min(0.0f, p.rangeDb)));
  attack_ = onePole(1e-3 * static_cast<double>(p.attackMs), fs_);
  release_ = onePole(1e-3 * static_cast<double>(p.releaseMs), fs_);
  holdSamples_ = static_cast<int32_t>(std::lround(1e-3 * static_cast<double>(p.holdMs) * fs_));
}

void NoiseGate::reset() noexcept {
  env_ = 0.0f;
  open_ = false;
  hold_ = 0;
  gain_ = floor_;
}

// [RT]
void NoiseGate::forceOpen() noexcept {
  open_ = true;
  hold_ = holdSamples_;
  gain_ = 1.0f;
}

// [RT]
void NoiseGate::process(float* x, uint32_t n) noexcept {
  for (uint32_t i = 0; i < n; ++i) {
    env_ = std::max(std::fabs(x[i]), env_ * envDecay_);
    if (!open_) {
      if (env_ >= openLevel_) {
        open_ = true;
        hold_ = holdSamples_;
      }
    } else if (env_ >= closeLevel_) {
      hold_ = holdSamples_;
    } else if (hold_ > 0) {
      --hold_;
    } else {
      open_ = false;
    }
    const float target = open_ ? 1.0f : floor_;
    gain_ += (target - gain_) * (target > gain_ ? attack_ : release_);
    x[i] *= gain_;
  }
}

}  // namespace tonecore
