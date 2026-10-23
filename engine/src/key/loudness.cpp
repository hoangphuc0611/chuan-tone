#include "key/loudness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace tonecore {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Nguyên mẫu analog của hai tầng K-weighting (khớp bảng hệ số 48 kHz của BS.1770-4 tới ~1e-9).
constexpr double kShelfF0 = 1681.974450955533;
constexpr double kShelfGainDb = 3.999843853973347;
constexpr double kShelfQ = 0.7071752369554196;
constexpr double kHighpassF0 = 38.13547087602444;
constexpr double kHighpassQ = 0.5003270373238773;

constexpr double kBlockSec = 0.4;
constexpr int kStepsPerBlock = 4;  // chồng 75%: bước 100 ms
constexpr double kAbsoluteGate = -70.0;
constexpr double kRelativeGate = -10.0;

double loudnessOf(double z) {
  return -0.691 + 10.0 * std::log10(z);
}

}  // namespace

BiquadCoeffs kWeightingShelf(double fs) {
  // Kệ cao bậc 2, biến đổi song tuyến với K = tan(π·f0/fs).
  const double k = std::tan(kPi * kShelfF0 / fs);
  const double vh = std::pow(10.0, kShelfGainDb / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const double a0 = 1.0 + k / kShelfQ + k * k;
  BiquadCoeffs c;
  c.b0 = (vh + vb * k / kShelfQ + k * k) / a0;
  c.b1 = 2.0 * (k * k - vh) / a0;
  c.b2 = (vh - vb * k / kShelfQ + k * k) / a0;
  c.a1 = 2.0 * (k * k - 1.0) / a0;
  c.a2 = (1.0 - k / kShelfQ + k * k) / a0;
  return c;
}

BiquadCoeffs kWeightingHighpass(double fs) {
  const double k = std::tan(kPi * kHighpassF0 / fs);
  const double a0 = 1.0 + k / kHighpassQ + k * k;
  BiquadCoeffs c;
  c.b0 = 1.0;
  c.b1 = -2.0;
  c.b2 = 1.0;
  c.a1 = 2.0 * (k * k - 1.0) / a0;
  c.a2 = (1.0 - k / kHighpassQ + k * k) / a0;
  return c;
}

LoudnessResult measureLoudness(const DecodedAudio& audio) {
  LoudnessResult out;
  const std::size_t ch = audio.channels;
  const auto frames = static_cast<std::size_t>(audio.frames);
  if (ch == 0 || frames == 0 || audio.sampleRate == 0 || audio.samples.size() < frames * ch) {
    return out;
  }
  // Kênh được đo và trọng số: mono → mono kép (×2); ≥ 2 kênh → 2 kênh đầu.
  const std::size_t measured = std::min<std::size_t>(ch, 2);
  const double weight = ch == 1 ? 2.0 : 1.0;

  const double fs = audio.sampleRate;
  std::vector<Biquad> shelf(measured);
  std::vector<Biquad> highpass(measured);
  for (std::size_t c = 0; c < measured; ++c) {
    shelf[c].setCoeffs(kWeightingShelf(fs));
    highpass[c].setCoeffs(kWeightingHighpass(fs));
  }

  // Năng lượng từng bước 100 ms (tổng bình phương, đã nhân trọng số kênh).
  const auto stepLen = static_cast<std::size_t>(std::lround(fs * kBlockSec / kStepsPerBlock));
  std::vector<double> steps;
  steps.reserve(frames / stepLen + 1);
  double acc = 0.0;
  std::size_t inStep = 0;
  int peak = 0;
  constexpr float kScale = 1.0f / 32768.0f;
  for (std::size_t i = 0; i < frames; ++i) {
    for (std::size_t c = 0; c < measured; ++c) {
      const int16_t s = audio.samples[i * ch + c];
      peak = std::max(peak, std::abs(static_cast<int>(s)));
      const float y = highpass[c].process(shelf[c].process(static_cast<float>(s) * kScale));
      acc += weight * static_cast<double>(y) * static_cast<double>(y);
    }
    if (++inStep == stepLen) {
      steps.push_back(acc);
      acc = 0.0;
      inStep = 0;
    }
  }
  out.samplePeakDb = peak > 0 ? static_cast<float>(20.0 * std::log10(peak / 32768.0)) : -200.0f;

  // Khối 400 ms = 4 bước liên tiếp; z = trung bình bình phương trên khối.
  const double blockLen = static_cast<double>(stepLen * kStepsPerBlock);
  std::vector<double> blocks;
  for (std::size_t s = 0; s + kStepsPerBlock <= steps.size(); ++s) {
    double z = 0.0;
    for (int j = 0; j < kStepsPerBlock; ++j) z += steps[s + static_cast<std::size_t>(j)];
    blocks.push_back(z / blockLen);
  }

  // Cổng tuyệt đối rồi cổng tương đối.
  double sum = 0.0;
  std::size_t n = 0;
  for (const double z : blocks) {
    if (z > 0.0 && loudnessOf(z) > kAbsoluteGate) {
      sum += z;
      ++n;
    }
  }
  if (n == 0) return out;
  const double relGate = loudnessOf(sum / static_cast<double>(n)) + kRelativeGate;
  sum = 0.0;
  n = 0;
  for (const double z : blocks) {
    if (z > 0.0) {
      const double l = loudnessOf(z);
      if (l > kAbsoluteGate && l > relGate) {
        sum += z;
        ++n;
      }
    }
  }
  if (n > 0) out.integratedLufs = static_cast<float>(loudnessOf(sum / static_cast<double>(n)));
  return out;
}

}  // namespace tonecore
