#include "pitch/corrector.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr double kGapResetSec = 0.06;
constexpr double kSettleSec = 0.048;
constexpr double kHumanizeStartSec = 0.12;
constexpr double kHumanizeRampSec = 0.5;
// Ngưỡng tách nốt (nửa cung) = 0,7 + 1,3·p. p = 0 → 0,7 (> rung giọng ±50 cent); p = 1 (Tự nhiên)
// → 2: luyến / dấu thanh trong âm tiết (tới ±2 nửa cung quanh tâm) vẫn là một nốt nên được dịch
// nguyên khối, không bị cắt vụn. Chọn bằng mô phỏng (tools: corrector sweep, xem
// docs/pitch-results.md).
constexpr float kNewNoteBase = 0.7f;
constexpr float kNewNotePerPreserve = 1.3f;
constexpr int kNewNoteFrames = 2;
}  // namespace

PitchCorrector::PitchCorrector(double frameRateHz)
    : dt_(1.0 / frameRateHz),
      settleFrames_(std::max(1, static_cast<int>(std::lround(kSettleSec * frameRateHz)))) {}

void PitchCorrector::startNote() noexcept {
  noteFrames_ = 0;
  histHead_ = 0;
  histCount_ = 0;
  histSum_ = 0.0;
  farFrames_ = 0;
}

void PitchCorrector::reset() noexcept {
  quantizer_.reset();
  started_ = false;
  gapFrames_ = 0;
  lastTarget_ = -1;
  held_ = 0.0;
  y1_ = y2_ = 0.0f;
  startNote();
}

// [RT]
CorrectionFrame PitchCorrector::update(const TrackedPitch& p) noexcept {
  CorrectionFrame out;
  if (!p.voiced) {
    // Lặng ngắn (phụ âm): giữ nguyên trạng thái — âm tiết sau cùng nốt thì chỉnh liền mạch, khác
    // nốt thì quy tắc tách nốt bên dưới sẽ tự nhận ra.
    if (started_ && ++gapFrames_ * dt_ > kGapResetSec) reset();
    out.correction = y2_;
    out.targetNote = lastTarget_;
    return out;
  }
  gapFrames_ = 0;
  started_ = true;
  const float m = p.midi;

  // 1–2. Tách nốt, trung bình nốt (bỏ đoạn vuốt đầu nốt).
  const float pv = std::clamp(params_.vibratoPreserve, 0.0f, 1.0f);
  if (histCount_ > 0) {
    const auto mu = static_cast<float>(histSum_ / histCount_);
    if (std::fabs(m - mu) > kNewNoteBase + kNewNotePerPreserve * pv) {
      if (++farFrames_ >= kNewNoteFrames) startNote();
    } else {
      farFrames_ = 0;
    }
  }
  ++noteFrames_;
  if (noteFrames_ > settleFrames_) {
    if (histCount_ == kWindow) {
      histSum_ -= static_cast<double>(hist_[histHead_]);
    } else {
      ++histCount_;
    }
    hist_[histHead_] = m;
    histSum_ += static_cast<double>(m);
    histHead_ = (histHead_ + 1) % kWindow;
  }
  const bool haveMean = histCount_ > 0;
  const float mu = haveMean ? static_cast<float>(histSum_ / histCount_) : m;

  // 3. Tâm dùng để chỉnh.
  const float center = pv * mu + (1.0f - pv) * m;

  // 4. Nốt đích và lượng chỉnh mong muốn.
  const int target = quantizer_.quantize(center, std::max(0.0f, params_.hysteresisCents));
  if (target == lastTarget_) {
    held_ += dt_;
  } else {
    held_ = 0.0;
    lastTarget_ = target;
  }
  float desired = static_cast<float>(target) - center;
  if (!haveMean) {
    // Đầu nốt (đoạn vuốt / đang chuyển nốt): phần "giữ tự nhiên" giữ nguyên lượng chỉnh đang áp,
    // không kéo đoạn chuyển động về lưới nốt và không làm cao độ chao khi đổi âm tiết.
    desired = (1.0f - pv) * desired + pv * y2_;
  }
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
  out.active = true;
  out.targetNote = target;
  out.inputMidi = m;
  out.centsOff = (m - static_cast<float>(target)) * 100.0f;
  out.correction = std::clamp(y2_, -maxSemi, maxSemi);
  return out;
}

}  // namespace tonecore
