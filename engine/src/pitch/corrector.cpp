#include "pitch/corrector.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr double kGapResetSec = 0.06;
constexpr double kHumanizeStartSec = 0.12;
constexpr double kHumanizeRampSec = 0.5;
}  // namespace

PitchCorrector::PitchCorrector(double frameRateHz) : dt_(1.0 / frameRateHz) {
  slow_.setCoeffs(BiquadCoeffs::lowpass(frameRateHz, 2.0));
}

void PitchCorrector::reset() noexcept {
  quantizer_.reset();
  slow_.reset();
  started_ = false;
  gapFrames_ = 0;
  lastTarget_ = -1;
  held_ = 0.0;
  y1_ = y2_ = 0.0f;
}

// [RT]
CorrectionFrame PitchCorrector::update(const TrackedPitch& p) noexcept {
  CorrectionFrame out;
  if (!p.voiced) {
    // Khoảng lặng ngắn: giữ trạng thái để nốt sau phụ âm không phải "trượt" lại từ đầu.
    if (started_ && ++gapFrames_ * dt_ > kGapResetSec) reset();
    out.correction = y2_;
    out.targetNote = lastTarget_;
    return out;
  }
  gapFrames_ = 0;
  const float m = p.midi;
  if (!started_) {
    slow_.resetSteady(m);
    y1_ = y2_ = 0.0f;
    held_ = 0.0;
    started_ = true;
  }

  // 1–2. Tách chậm/nhanh, tính tâm.
  const float s = slow_.process(m);
  const float pv = std::clamp(params_.vibratoPreserve, 0.0f, 1.0f);
  const float center = m - pv * (m - s);

  // 3. Nốt đích.
  const int target = quantizer_.quantize(center, std::max(0.0f, params_.hysteresisCents));
  if (target == lastTarget_) {
    held_ += dt_;
  } else {
    held_ = 0.0;
    lastTarget_ = target;
  }

  // 4. Lượng chỉnh mong muốn.
  float desired = static_cast<float>(target) - center;
  if (std::fabs(desired) * 100.0f < params_.flexCents) desired = 0.0f;

  // 5. Làm mượt bậc 2, retune hiệu dụng theo humanize.
  const double h = std::clamp(static_cast<double>(params_.humanize), 0.0, 1.0);
  const double ramp = std::clamp((held_ - kHumanizeStartSec) / kHumanizeRampSec, 0.0, 1.0);
  const double retune = std::clamp(static_cast<double>(params_.retuneMs), 0.0, 200.0) * 1e-3;
  const double eff = retune * (1.0 + 3.0 * h * ramp) + 0.06 * h * ramp;
  if (eff < 0.5e-3) {
    y1_ = y2_ = desired;
  } else {
    const auto a = static_cast<float>(1.0 - std::exp(-dt_ / (eff / 4.0)));
    y1_ += a * (desired - y1_);
    y2_ += a * (y1_ - y2_);
  }

  // 6. Kẹp.
  const float maxSemi = std::max(0.0f, params_.maxCorrectionCents) / 100.0f;
  const float corr = std::clamp(y2_, -maxSemi, maxSemi);

  out.active = true;
  out.targetNote = target;
  out.inputMidi = m;
  out.centsOff = (m - static_cast<float>(target)) * 100.0f;
  out.correction = corr;
  return out;
}

}  // namespace tonecore
