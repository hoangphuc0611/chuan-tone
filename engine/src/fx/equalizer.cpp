#include "fx/equalizer.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr double kButterworthQ = 0.70710678118654752;
// Tầng bell / shelf được coi là phẳng (bỏ qua để tiết kiệm CPU) khi |dB| dưới mức này và đã đứng
// yên.
constexpr double kFlatDb = 1e-4;
}  // namespace

Equalizer::Equalizer(double sampleRate, double smoothingSec)
    : fs_(sampleRate),
      smooth_(smoothingSec <= 0.0
                  ? 1.0
                  : 1.0 - std::exp(-static_cast<double>(kSubBlock) / (smoothingSec * sampleRate))) {
  for (auto& s : stages_) {
    s.logFreq = s.logFreqTarget = std::log(1000.0);
    updateCoeffs(s);
  }
}

void Equalizer::setStageTarget(Stage& s, SvfType type, double freq, double gainDb,
                               double q) noexcept {
  const double logF = std::log(std::max(freq, 1.0));
  if (type != s.type) {
    // Bật / tắt / đổi loại: áp ngay, bắt đầu từ đúng đích (không trượt từ thông số của loại cũ).
    s.type = type;
    s.logFreq = logF;
    s.gainDb = gainDb;
    s.q = q;
    s.f.reset();
  }
  s.logFreqTarget = logF;
  s.gainTarget = gainDb;
  s.qTarget = q;
  s.moving = s.logFreq != logF || s.gainDb != gainDb || s.q != q;
  updateCoeffs(s);
}

void Equalizer::setParams(const EqParams& p) noexcept {
  const auto hp = static_cast<double>(p.highPassHz);
  const auto lp = static_cast<double>(p.lowPassHz);
  setStageTarget(stages_[0], hp > 0.0 ? SvfType::HighPass : SvfType::Off, hp > 0.0 ? hp : 20.0, 0.0,
                 kButterworthQ);
  for (int b = 0; b < EqParams::kBands; ++b) {
    const EqBand& band = p.bands[b];
    setStageTarget(stages_[b + 1], band.type, static_cast<double>(band.freqHz),
                   static_cast<double>(band.gainDb), static_cast<double>(band.q));
  }
  setStageTarget(stages_[kStages - 1], lp > 0.0 ? SvfType::LowPass : SvfType::Off,
                 lp > 0.0 ? lp : 20000.0, 0.0, kButterworthQ);
}

void Equalizer::setParamsImmediate(const EqParams& p) noexcept {
  setParams(p);
  for (auto& s : stages_) {
    s.logFreq = s.logFreqTarget;
    s.gainDb = s.gainTarget;
    s.q = s.qTarget;
    s.moving = false;
    updateCoeffs(s);
  }
}

void Equalizer::reset() noexcept {
  for (auto& s : stages_) s.f.reset();
}

void Equalizer::updateCoeffs(Stage& s) noexcept {
  s.c = SvfCoeffs::make(s.type, fs_, std::exp(s.logFreq), s.q, s.gainDb);
}

bool Equalizer::active(const Stage& s) const noexcept {
  switch (s.type) {
    case SvfType::Off:
      return false;
    case SvfType::Bell:
    case SvfType::LowShelf:
    case SvfType::HighShelf:
      return s.moving || std::fabs(s.gainDb) > kFlatDb;
    default:
      return true;
  }
}

// [RT]
void Equalizer::process(float* x, uint32_t n) noexcept {
  for (uint32_t start = 0; start < n; start += kSubBlock) {
    const uint32_t len = std::min(kSubBlock, n - start);
    float* blk = x + start;
    for (auto& s : stages_) {
      if (s.moving) {
        // Trượt một bước (một cực) về đích; đủ gần thì chốt đúng đích.
        s.logFreq += smooth_ * (s.logFreqTarget - s.logFreq);
        s.gainDb += smooth_ * (s.gainTarget - s.gainDb);
        s.q += smooth_ * (s.qTarget - s.q);
        if (std::fabs(s.logFreq - s.logFreqTarget) < 1e-5 &&
            std::fabs(s.gainDb - s.gainTarget) < 1e-4 && std::fabs(s.q - s.qTarget) < 1e-5) {
          s.logFreq = s.logFreqTarget;
          s.gainDb = s.gainTarget;
          s.q = s.qTarget;
          s.moving = false;
        }
        updateCoeffs(s);
      }
      if (!active(s)) continue;
      s.f.processBlock(blk, len, s.c);
    }
  }
}

double Equalizer::magnitudeAt(double f) const noexcept {
  double m = 1.0;
  for (const auto& s : stages_) {
    if (s.type != SvfType::Off) m *= s.c.magnitudeAt(f, fs_);
  }
  return m;
}

}  // namespace tonecore
