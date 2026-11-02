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

LoudnessMeter::LoudnessMeter(int channels, double sampleRate)
    : channels_(static_cast<std::size_t>(std::max(1, channels))),
      measured_(std::min<std::size_t>(channels_, 2)),
      // Mono → mono kép (×2); ≥ 2 kênh → 2 kênh đầu.
      weight_(channels_ == 1 ? 2.0 : 1.0),
      shelf_(measured_),
      highpass_(measured_),
      stepLen_(static_cast<std::size_t>(std::lround(sampleRate * kBlockSec / kStepsPerBlock))) {
  for (std::size_t c = 0; c < measured_; ++c) {
    shelf_[c].setCoeffs(kWeightingShelf(sampleRate));
    highpass_[c].setCoeffs(kWeightingHighpass(sampleRate));
  }
}

void LoudnessMeter::process(const float* x, std::size_t frames) {
  // Năng lượng từng bước 100 ms (tổng bình phương, đã nhân trọng số kênh).
  for (std::size_t i = 0; i < frames; ++i) {
    for (std::size_t c = 0; c < measured_; ++c) {
      const float s = x[i * channels_ + c];
      peak_ = std::max(peak_, std::abs(s));
      const float y = highpass_[c].process(shelf_[c].process(s));
      acc_ += weight_ * static_cast<double>(y) * static_cast<double>(y);
    }
    if (++inStep_ == stepLen_) {
      steps_.push_back(acc_);
      acc_ = 0.0;
      inStep_ = 0;
    }
  }
}

LoudnessResult LoudnessMeter::result() const {
  LoudnessResult out;
  out.samplePeakDb = peak_ > 0.0f ? 20.0f * std::log10(peak_) : -200.0f;
  // Khối 400 ms = 4 bước liên tiếp; z = trung bình bình phương trên khối.
  const double blockLen = static_cast<double>(stepLen_ * kStepsPerBlock);
  std::vector<double> blocks;
  for (std::size_t s = 0; s + kStepsPerBlock <= steps_.size(); ++s) {
    double z = 0.0;
    for (int j = 0; j < kStepsPerBlock; ++j) z += steps_[s + static_cast<std::size_t>(j)];
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

LoudnessResult measureLoudness(const DecodedAudio& audio) {
  const std::size_t ch = audio.channels;
  const auto frames = static_cast<std::size_t>(audio.frames);
  if (ch == 0 || frames == 0 || audio.sampleRate == 0 || audio.samples.size() < frames * ch) {
    return LoudnessResult{};
  }
  LoudnessMeter meter(static_cast<int>(ch), audio.sampleRate);
  constexpr std::size_t kChunk = 4096;
  constexpr float kScale = 1.0f / 32768.0f;
  std::vector<float> buf(kChunk * ch);
  for (std::size_t at = 0; at < frames; at += kChunk) {
    const std::size_t n = std::min(kChunk, frames - at);
    for (std::size_t i = 0; i < n * ch; ++i) {
      buf[i] = static_cast<float>(audio.samples[at * ch + i]) * kScale;
    }
    meter.process(buf.data(), n);
  }
  return meter.result();
}

}  // namespace tonecore
