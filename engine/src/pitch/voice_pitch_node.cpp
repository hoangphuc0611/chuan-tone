#include "pitch/voice_pitch_node.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

constexpr double kHpfHz = 80.0;
constexpr double kMixFadeSec = 0.02;

PitchAnalyzerConfig analyzerConfig(double sampleRate) {
  PitchAnalyzerConfig c;
  c.inputRate = sampleRate;
  return c;
}

PsolaConfig psolaConfig(double sampleRate) {
  PsolaConfig c;
  c.sampleRate = sampleRate;
  c.latency = static_cast<int>(std::lround(0.010 * sampleRate));  // 10 ms
  c.crossfade = static_cast<int>(std::lround(0.005 * sampleRate));
  return c;
}

}  // namespace

VoicePitchNode::VoicePitchNode(double sampleRate, uint32_t maxBlockFrames)
    : sampleRate_(sampleRate),
      analyzer_(analyzerConfig(sampleRate)),
      corrector_(1000.0 / analyzerConfig(sampleRate).hopMs),
      psola_(psolaConfig(sampleRate)),
      wet_(maxBlockFrames, 0.0f),
      slice_(static_cast<uint32_t>(analyzer_.hop()) *
             static_cast<uint32_t>(std::lround(sampleRate / analyzer_.analysisRate()))),
      mixStep_(static_cast<float>(1.0 / (kMixFadeSec * sampleRate))) {
  hpf_.setCoeffs(BiquadCoeffs::highpass(sampleRate, kHpfHz));
  const CorrectorParams p = correctionPreset(CorrectionPreset::Singer);
  retuneMs_.store(p.retuneMs);
  humanize_.store(p.humanize);
  flexCents_.store(p.flexCents);
  vibratoPreserve_.store(p.vibratoPreserve);
  hysteresisCents_.store(p.hysteresisCents);
  corrector_.setParams(p);
  corrector_.setKeyMask(keyMask(0, Scale::Chromatic));
  if (slice_ == 0) slice_ = 192;
}

// ---- Luồng điều khiển ----

void VoicePitchNode::setKey(int root, Scale scale) noexcept {
  keyRoot_.store(((root % 12) + 12) % 12, std::memory_order_relaxed);
  keyScale_.store(static_cast<int>(scale), std::memory_order_relaxed);
  paramVersion_.fetch_add(1, std::memory_order_release);
}

void VoicePitchNode::setTuningCents(float cents) noexcept {
  tuningCents_.store(std::isfinite(cents) ? std::clamp(cents, -100.0f, 100.0f) : 0.0f,
                     std::memory_order_relaxed);
  paramVersion_.fetch_add(1, std::memory_order_release);
}

void VoicePitchNode::applyPreset(CorrectionPreset preset) noexcept {
  const CorrectorParams p = correctionPreset(preset);
  retuneMs_.store(p.retuneMs, std::memory_order_relaxed);
  humanize_.store(p.humanize, std::memory_order_relaxed);
  flexCents_.store(p.flexCents, std::memory_order_relaxed);
  vibratoPreserve_.store(p.vibratoPreserve, std::memory_order_relaxed);
  hysteresisCents_.store(p.hysteresisCents, std::memory_order_relaxed);
  paramVersion_.fetch_add(1, std::memory_order_release);
}

#define TC_PITCH_SETTER(name, member, lo, hi)                                               \
  void VoicePitchNode::name(float v) noexcept {                                             \
    member.store(std::isfinite(v) ? std::clamp(v, lo, hi) : lo, std::memory_order_relaxed); \
    paramVersion_.fetch_add(1, std::memory_order_release);                                  \
  }
TC_PITCH_SETTER(setRetuneMs, retuneMs_, 0.0f, 200.0f)
TC_PITCH_SETTER(setHumanize, humanize_, 0.0f, 1.0f)
TC_PITCH_SETTER(setFlexCents, flexCents_, 0.0f, 100.0f)
TC_PITCH_SETTER(setVibratoPreserve, vibratoPreserve_, 0.0f, 1.0f)
TC_PITCH_SETTER(setHysteresisCents, hysteresisCents_, 0.0f, 50.0f)
#undef TC_PITCH_SETTER

CorrectorParams VoicePitchNode::params() const noexcept {
  CorrectorParams p;
  p.retuneMs = retuneMs_.load(std::memory_order_relaxed);
  p.humanize = humanize_.load(std::memory_order_relaxed);
  p.flexCents = flexCents_.load(std::memory_order_relaxed);
  p.vibratoPreserve = vibratoPreserve_.load(std::memory_order_relaxed);
  p.hysteresisCents = hysteresisCents_.load(std::memory_order_relaxed);
  return p;
}

// ---- Audio thread ----

// [RT]
uint16_t VoicePitchNode::activeKeyMask() const noexcept {
  if (overrideActive_) return keyMask(overrideRoot_, overrideScale_);
  return keyMask(keyRoot_.load(std::memory_order_relaxed),
                 static_cast<Scale>(keyScale_.load(std::memory_order_relaxed)));
}

// [RT]
void VoicePitchNode::syncParams() noexcept {
  const uint32_t v = paramVersion_.load(std::memory_order_acquire);
  if (v == seenVersion_) return;
  seenVersion_ = v;
  corrector_.setParams(params());
  corrector_.setKeyMask(activeKeyMask());
  tuningSemis_ = tuningCents_.load(std::memory_order_relaxed) / 100.0f;
}

// [RT]
void VoicePitchNode::setKeyOverride(bool active, int root, Scale scale) noexcept {
  overrideActive_ = active;
  overrideRoot_ = ((root % 12) + 12) % 12;
  overrideScale_ = scale;
  corrector_.setKeyMask(activeKeyMask());
}

// [RT] Một khung phân tích xong → cập nhật corrector, PSOLA và trạng thái cho UI.
void VoicePitchNode::onFrame(const PitchFrame& f) noexcept {
  // Lượng tử trên lưới của beat: dời cao độ người hát ngược với tuning, lượng chỉnh (nửa cung)
  // không đổi khi dời cả lưới lẫn cao độ.
  TrackedPitch onGrid = f.tracked;
  if (onGrid.voiced) onGrid.midi -= tuningSemis_;
  const CorrectionFrame c = corrector_.update(onGrid);
  const bool voiced = f.tracked.voiced;
  psola_.setAnalysis(
      voiced, voiced ? static_cast<float>(sampleRate_ / static_cast<double>(f.tracked.f0)) : 0.0f);
  psola_.setRatio(std::exp2(c.correction / 12.0f));
  snap_.voiced = voiced;
  snap_.f0 = voiced ? f.tracked.f0 : 0.0f;
  snap_.confidence = f.tracked.confidence;
  snap_.inputMidi = voiced ? f.tracked.midi : 0.0f;
  snap_.targetNote = c.targetNote;
  snap_.centsOff = voiced ? c.centsOff : 0.0f;
  snap_.correctionCents = c.active ? c.correction * 100.0f : 0.0f;
}

// [RT]
void VoicePitchNode::processSlice(float* io, uint32_t n) noexcept {
  hpf_.process(io, io, n);
  analyzer_.process(io, n, [this](const PitchFrame& f) { onFrame(f); });
  float* wet = wet_.data();
  psola_.process(io, wet, n);

  const float target = enabled() ? 1.0f : 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    if (mix_ < target) {
      mix_ = std::min(target, mix_ + mixStep_);
    } else if (mix_ > target) {
      mix_ = std::max(target, mix_ - mixStep_);
    }
    io[i] += mix_ * (wet[i] - io[i]);
  }
}

// [RT]
void VoicePitchNode::process(float* io, uint32_t n) noexcept {
  syncParams();
  uint32_t done = 0;
  while (done < n) {
    const uint32_t len = std::min(slice_, n - done);
    processSlice(io + done, len);
    done += len;
  }
  state_.publish(snap_);
}

}  // namespace tonecore
