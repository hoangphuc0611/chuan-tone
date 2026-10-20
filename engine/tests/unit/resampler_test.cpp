#include "dsp/resampler.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using tonecore::Resampler;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double freq, double rate, std::size_t frames, int channels = 1) {
  std::vector<float> v(frames * static_cast<std::size_t>(channels));
  for (std::size_t i = 0; i < frames; ++i) {
    const auto s = static_cast<float>(std::sin(2.0 * kPi * freq * static_cast<double>(i) / rate));
    for (int c = 0; c < channels; ++c) v[i * static_cast<std::size_t>(channels) + c] = s;
  }
  return v;
}

double db(double ratio) {
  return 20.0 * std::log10(ratio);
}

}  // namespace

TEST(Resampler, OutputLength) {
  Resampler r(44100.0, 48000.0);
  EXPECT_EQ(r.outputFrames(44100), 48000u);
  EXPECT_EQ(r.outputFrames(0), 0u);
  EXPECT_NEAR(r.ratio(), 48000.0 / 44100.0, 1e-12);
}

// 1 kHz 44,1 → 48 kHz: so với sin lý tưởng ở 48 kHz, sai số phải dưới −70 dB.
TEST(Resampler, UpsamplePreservesSineAccurately) {
  const std::size_t inFrames = 44100;
  const auto in = sine(1000.0, 44100.0, inFrames);
  Resampler r(44100.0, 48000.0);
  const auto out = r.process(in.data(), inFrames, 1);
  ASSERT_EQ(out.size(), 48000u);

  const auto ideal = sine(1000.0, 48000.0, out.size());
  double err = 0.0;
  double sig = 0.0;
  // Bỏ 100 ms hai đầu (hiệu ứng biên do giả định ngoài đoạn là 0).
  for (std::size_t i = 4800; i < out.size() - 4800; ++i) {
    err += (out[i] - ideal[i]) * (out[i] - ideal[i]);
    sig += ideal[i] * ideal[i];
  }
  EXPECT_LT(db(std::sqrt(err / sig)), -70.0);
}

// 48 → 44,1 kHz với sin 23,5 kHz (trên Nyquist mới 22,05 kHz): phải bị chặn, không alias.
TEST(Resampler, DownsampleRejectsAliasing) {
  const std::size_t inFrames = 48000;
  const auto in = sine(23500.0, 48000.0, inFrames);
  Resampler r(48000.0, 44100.0);
  const auto out = r.process(in.data(), inFrames, 1);

  double sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = 4410; i < out.size() - 4410; ++i, ++n) sum += out[i] * out[i];
  const double rms = std::sqrt(sum / static_cast<double>(n));
  EXPECT_LT(db(rms / (1.0 / std::sqrt(2.0))), -70.0);
}

// Tần số trong dải thông giữ biên độ (gợn dải thông nhỏ).
TEST(Resampler, PassbandGainIsFlat) {
  for (double f : {100.0, 5000.0, 15000.0, 19000.0}) {
    const auto in = sine(f, 44100.0, 44100);
    Resampler r(44100.0, 48000.0);
    const auto out = r.process(in.data(), 44100, 1);
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 4800; i < out.size() - 4800; ++i, ++n) sum += out[i] * out[i];
    const double gainDb = db(std::sqrt(sum / static_cast<double>(n)) * std::sqrt(2.0));
    EXPECT_NEAR(gainDb, 0.0, 0.1) << "f=" << f;
  }
}

TEST(Resampler, StereoChannelsStayIndependent) {
  const std::size_t frames = 4410;
  std::vector<float> in(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    in[2 * i] = 0.5f;        // trái: DC
    in[2 * i + 1] = -0.25f;  // phải: DC khác
  }
  Resampler r(44100.0, 48000.0);
  const auto out = r.process(in.data(), frames, 2);
  const std::size_t mid = out.size() / 4;  // frame ở giữa
  EXPECT_NEAR(out[2 * mid], 0.5f, 1e-3f);
  EXPECT_NEAR(out[2 * mid + 1], -0.25f, 1e-3f);
}

TEST(Resampler, SameRateIsNearIdentity) {
  const auto in = sine(440.0, 48000.0, 4800);
  Resampler r(48000.0, 48000.0);
  const auto out = r.process(in.data(), 4800, 1);
  ASSERT_EQ(out.size(), in.size());
  for (std::size_t i = 200; i < 4600; ++i) EXPECT_NEAR(out[i], in[i], 1e-3f);
}
