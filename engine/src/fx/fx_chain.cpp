#include "fx/fx_chain.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

bool FxPreset::eqOn() const noexcept {
  if (eq.highPassHz > 0.0f || eq.lowPassHz > 0.0f) return true;
  for (const auto& b : eq.bands) {
    if (b.type != SvfType::Off) return true;
  }
  return false;
}

bool needsRebuild(const FxPreset& a, const FxPreset& b) noexcept {
  if (a.eqOn() != b.eqOn() || a.compOn != b.compOn || a.deessOn != b.deessOn ||
      a.reverbOn != b.reverbOn) {
    return true;
  }
  if ((a.eq.highPassHz > 0.0f) != (b.eq.highPassHz > 0.0f) ||
      (a.eq.lowPassHz > 0.0f) != (b.eq.lowPassHz > 0.0f)) {
    return true;
  }
  for (int i = 0; i < EqParams::kBands; ++i) {
    if (a.eq.bands[i].type != b.eq.bands[i].type) return true;
  }
  // Delay: bật / tắt, thời gian (đổi khi đang chạy → cao độ tiếng vọng trượt), bộ lọc bật / tắt.
  if (a.delay.division != b.delay.division) return true;
  if (a.delay.division != NoteDivision::Off &&
      (Delay::delaySeconds(a.delay) != Delay::delaySeconds(b.delay) ||
       (a.delay.lowPassHz > 0.0f) != (b.delay.lowPassHz > 0.0f) ||
       (a.delay.highPassHz > 0.0f) != (b.delay.highPassHz > 0.0f))) {
    return true;
  }
  // Reverb: kiểu phòng (độ dài dòng), pre-delay.
  if (a.reverbOn &&
      (a.reverb.type != b.reverb.type || a.reverb.preDelayMs != b.reverb.preDelayMs)) {
    return true;
  }
  return false;
}

FxChain::FxChain(double sampleRate, uint32_t maxBlockFrames, const FxPreset& preset,
                 uint64_t generation)
    : preset_(preset),
      generation_(generation),
      eqOn_(preset.eqOn()),
      delayOn_(preset.delay.division != NoteDivision::Off),
      eq_(sampleRate),
      comp_(sampleRate),
      deess_(sampleRate),
      delay_(sampleRate),
      reverb_(sampleRate),
      dry_(maxBlockFrames, 0.0f),
      send_(maxBlockFrames, 0.0f),
      echo_(maxBlockFrames, 0.0f),
      revIn_(maxBlockFrames, 0.0f),
      wetL_(maxBlockFrames, 0.0f),
      wetR_(maxBlockFrames, 0.0f) {
  eq_.setParamsImmediate(preset.eq);
  comp_.setParams(preset.comp);
  comp_.reset();
  deess_.setParams(preset.deess);
  deess_.reset();
  delay_.setParams(preset.delay);
  delay_.reset();
  reverb_.setParams(preset.reverb);
  reverb_.reset();
}

void FxChain::applyContinuous(const FxPreset& p) noexcept {
  preset_ = p;
  if (eqOn_) eq_.setParams(p.eq);
  if (p.compOn) comp_.setParams(p.comp);
  if (p.deessOn) deess_.setParams(p.deess);
  if (delayOn_) delay_.setParams(p.delay);
  if (p.reverbOn) reverb_.setParams(p.reverb);
}

// [RT]
void FxChain::process(const float* in, uint32_t n, bool withDry, float sendFrom,
                      float sendTo) noexcept {
  const bool active = in != nullptr && withDry;
  if (active) {
    std::copy(in, in + n, dry_.begin());
    if (eqOn_) eq_.process(dry_.data(), n);
    if (preset_.compOn) comp_.process(dry_.data(), n);
    if (preset_.deessOn) deess_.process(dry_.data(), n);
  } else {
    std::fill(dry_.begin(), dry_.begin() + n, 0.0f);
  }
  if (!hasWet()) {
    std::fill(wetL_.begin(), wetL_.begin() + n, 0.0f);
    std::fill(wetR_.begin(), wetR_.begin() + n, 0.0f);
    wetMs_ = 0.0f;
    return;
  }
  // Gửi sang phần ướt: khô · g, g trượt tuyến tính trong khối (tắt dần khi chuỗi đang bị thay).
  const float step = n > 0 ? (sendTo - sendFrom) / static_cast<float>(n) : 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    send_[i] = active ? dry_[i] * (sendFrom + step * static_cast<float>(i + 1)) : 0.0f;
  }
  if (delayOn_) {
    delay_.process(send_.data(), echo_.data(), n);
  } else {
    std::fill(echo_.begin(), echo_.begin() + n, 0.0f);
  }
  if (preset_.reverbOn) {
    for (uint32_t i = 0; i < n; ++i) revIn_[i] = send_[i] + echo_[i];
    reverb_.process(revIn_.data(), wetL_.data(), wetR_.data(), n);
  } else {
    std::fill(wetL_.begin(), wetL_.begin() + n, 0.0f);
    std::fill(wetR_.begin(), wetR_.begin() + n, 0.0f);
  }
  float e = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    wetL_[i] += echo_[i];
    wetR_[i] += echo_[i];
    e += wetL_[i] * wetL_[i] + wetR_[i] * wetR_[i];
  }
  wetMs_ = n > 0 ? e / static_cast<float>(2 * n) : 0.0f;
}

}  // namespace tonecore
