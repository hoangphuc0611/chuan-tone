#include "fx/delay.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr double kSmoothSec = 0.020;
constexpr double kButterworthQ = 0.70710678118654752;
}  // namespace

Delay::Delay(double sampleRate)
    : fs_(sampleRate),
      smooth_(static_cast<float>(1.0 - std::exp(-1.0 / (kSmoothSec * sampleRate)))) {
  uint32_t cap = 1;
  while (cap < static_cast<uint32_t>(kMaxSeconds * sampleRate) + 8) cap <<= 1;
  buf_.assign(cap, 0.0f);
  mask_ = cap - 1;
  setParams(DelayParams{});
  feedback_ = feedbackTarget_;
  mix_ = mixTarget_;
}

double Delay::delaySeconds(const DelayParams& p) noexcept {
  const double beat = 60.0 / std::max(30.0, static_cast<double>(p.bpm));
  switch (p.division) {
    case NoteDivision::Quarter:
      return beat;
    case NoteDivision::Eighth:
      return 0.5 * beat;
    case NoteDivision::DottedEighth:
      return 0.75 * beat;
    case NoteDivision::Sixteenth:
      return 0.25 * beat;
    case NoteDivision::Milliseconds:
      return 1e-3 * static_cast<double>(p.timeMs);
    case NoteDivision::Off:
      break;
  }
  return 0.0;
}

void Delay::setParams(const DelayParams& p) noexcept {
  off_ = p.division == NoteDivision::Off;
  delay_ = std::clamp(delaySeconds(p) * fs_, 2.0, kMaxSeconds * fs_);
  feedbackTarget_ = std::clamp(p.feedback, 0.0f, kMaxFeedback);
  mixTarget_ = off_ ? 0.0f : std::max(0.0f, p.mix);
  useLpf_ = p.lowPassHz > 0.0f;
  useHpf_ = p.highPassHz > 0.0f;
  if (useLpf_) {
    lpf_ = SvfCoeffs::make(SvfType::LowPass, fs_, static_cast<double>(p.lowPassHz), kButterworthQ,
                           0.0);
  }
  if (useHpf_) {
    hpf_ = SvfCoeffs::make(SvfType::HighPass, fs_, static_cast<double>(p.highPassHz), kButterworthQ,
                           0.0);
  }
}

void Delay::reset() noexcept {
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  lpfState_.reset();
  hpfState_.reset();
  feedback_ = feedbackTarget_;
  mix_ = mixTarget_;
}

// [RT]
void Delay::process(const float* in, float* wet, uint32_t n) noexcept {
  // Vị trí đọc = ghi − D: phần nguyên k (lùi) + phần lẻ u ∈ [0, 1).
  const double dk = std::floor(delay_);
  const auto k = static_cast<uint32_t>(dk);
  const auto u = static_cast<float>(delay_ - dk);
  // Lagrange bậc 3 qua 4 điểm tại độ trễ k−1, k, k+1, k+2 (trễ phân số k + u, Smith "Physical Audio
  // Signal Processing", Lagrange interpolation): trọng số theo d = u + 1 ∈ [1, 2).
  const float d = u + 1.0f;
  const float h0 = -(d - 1.0f) * (d - 2.0f) * (d - 3.0f) / 6.0f;
  const float h1 = d * (d - 2.0f) * (d - 3.0f) / 2.0f;
  const float h2 = -d * (d - 1.0f) * (d - 3.0f) / 2.0f;
  const float h3 = d * (d - 1.0f) * (d - 2.0f) / 6.0f;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t base = write_ - k + 1;  // độ trễ k − 1
    const float r = h0 * buf_[base & mask_] + h1 * buf_[(base - 1) & mask_] +
                    h2 * buf_[(base - 2) & mask_] + h3 * buf_[(base - 3) & mask_];
    float f = r;
    if (useLpf_) f = lpfState_.process(f, lpf_);
    if (useHpf_) f = hpfState_.process(f, hpf_);
    feedback_ += smooth_ * (feedbackTarget_ - feedback_);
    mix_ += smooth_ * (mixTarget_ - mix_);
    buf_[write_ & mask_] = in[i] + feedback_ * f;
    ++write_;
    wet[i] = mix_ * f;
  }
}

}  // namespace tonecore
