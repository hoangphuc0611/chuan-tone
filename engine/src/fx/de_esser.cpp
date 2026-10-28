#include "fx/de_esser.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

float decay(double timeSec, double fs) {
  return timeSec <= 0.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / (timeSec * fs)));
}
}  // namespace

DeEsser::DeEsser(double sampleRate)
    : fs_(sampleRate), rmsCoeff_(1.0f - decay(kRmsSec, sampleRate)) {
  setParams(DeEsserParams{});
}

void DeEsser::setParams(const DeEsserParams& p) noexcept {
  p_ = p;
  p_.ratio = std::max(1.0f, p.ratio);
  p_.maxReductionDb = std::max(0.0f, p.maxReductionDb);
  const double lo = static_cast<double>(std::max(100.0f, p.detectLowHz));
  const double hi = std::max(lo * 1.05, static_cast<double>(p.detectHighHz));
  const double center = std::sqrt(lo * hi);
  detect_ = SvfCoeffs::make(SvfType::BandPass, fs_, center, center / (hi - lo), 0.0);
  shelf_ = SvfCoeffs::make(SvfType::HighShelf, fs_, static_cast<double>(p_.shelfHz),
                           static_cast<double>(p_.shelfQ), static_cast<double>(shelfGainDb_));
  attack_ = decay(1e-3 * static_cast<double>(p.attackMs), fs_);
  release_ = decay(1e-3 * static_cast<double>(p.releaseMs), fs_);
}

void DeEsser::reset() noexcept {
  shelfFilter_.reset();
  detectFilter_.reset();
  ms_ = 0.0f;
  gs_ = 0.0f;
  shelfGainDb_ = 0.0f;
  shelf_ = SvfCoeffs::make(SvfType::HighShelf, fs_, static_cast<double>(p_.shelfHz),
                           static_cast<double>(p_.shelfQ), 0.0);
}

// [RT]
void DeEsser::process(float* x, uint32_t n) noexcept {
  const float t = p_.thresholdDb;
  const float slope = 1.0f / p_.ratio - 1.0f;  // ≤ 0
  const float floor = -p_.maxReductionDb;
  for (uint32_t start = 0; start < n; start += kSubBlock) {
    const uint32_t len = std::min(kSubBlock, n - start);
    float* blk = x + start;
    for (uint32_t i = 0; i < len; ++i) {
      const float s = detectFilter_.process(blk[i], detect_);
      ms_ += rmsCoeff_ * (s * s - ms_);
      const float level = 10.0f * std::log10(2.0f * ms_ + 1e-12f);
      const float gc = level > t ? std::max(floor, slope * (level - t)) : 0.0f;
      gs_ = gc < gs_ ? attack_ * gs_ + (1.0f - attack_) * gc
                     : release_ * gs_ + (1.0f - release_) * gc;
    }
    // Shelf theo mức giảm của khối này (đã làm mượt); gần 0 thì chốt đúng 0 → đi thẳng.
    const float g = gs_ > -1e-3f ? 0.0f : gs_;
    if (g != shelfGainDb_) {
      shelfGainDb_ = g;
      shelf_ = SvfCoeffs::make(SvfType::HighShelf, fs_, static_cast<double>(p_.shelfHz),
                               static_cast<double>(p_.shelfQ), static_cast<double>(g));
    }
    shelfFilter_.processBlock(blk, len, shelf_);
  }
}

}  // namespace tonecore
