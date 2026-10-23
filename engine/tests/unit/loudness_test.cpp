#include "key/loudness.h"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace tonecore;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Sin 1 kHz, mỗi đoạn (mức dBFS, số giây), giống nhau ở mọi kênh.
DecodedAudio sineSegments(uint32_t rate, uint32_t channels,
                          const std::vector<std::pair<double, double>>& segments) {
  DecodedAudio a;
  a.sampleRate = rate;
  a.channels = channels;
  std::size_t n = 0;
  for (const auto& [db, sec] : segments) {
    const double amp = std::pow(10.0, db / 20.0);
    const auto len = static_cast<std::size_t>(std::lround(sec * rate));
    for (std::size_t i = 0; i < len; ++i, ++n) {
      const double v = amp * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(n) / rate);
      const auto s = static_cast<int16_t>(std::lround(v * 32767.0));
      for (uint32_t c = 0; c < channels; ++c) a.samples.push_back(s);
    }
  }
  a.frames = n;
  return a;
}

}  // namespace

// Hệ số tính từ nguyên mẫu analog phải khớp bảng 48 kHz của ITU-R BS.1770-4.
TEST(Loudness, KWeightingMatchesStandardAt48k) {
  const BiquadCoeffs s = kWeightingShelf(48000.0);
  EXPECT_NEAR(s.b0, 1.53512485958697, 1e-8);
  EXPECT_NEAR(s.b1, -2.69169618940638, 1e-8);
  EXPECT_NEAR(s.b2, 1.19839281085285, 1e-8);
  EXPECT_NEAR(s.a1, -1.69065929318241, 1e-8);
  EXPECT_NEAR(s.a2, 0.73248077421585, 1e-8);
  const BiquadCoeffs h = kWeightingHighpass(48000.0);
  EXPECT_DOUBLE_EQ(h.b0, 1.0);
  EXPECT_DOUBLE_EQ(h.b1, -2.0);
  EXPECT_DOUBLE_EQ(h.b2, 1.0);
  EXPECT_NEAR(h.a1, -1.99004745483398, 1e-8);
  EXPECT_NEAR(h.a2, 0.99007225036621, 1e-8);
}

// Các ca test của EBU Tech 3341 (sin 1 kHz stereo), sai số cho phép ±0,1 LU.
TEST(Loudness, EbuTech3341Cases) {
  struct Case {
    std::vector<std::pair<double, double>> segments;
    double want;
  };
  const Case cases[] = {
      {{{-23.0, 20.0}}, -23.0},                                // ca 1
      {{{-33.0, 20.0}}, -33.0},                                // ca 2
      {{{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}}, -23.0},  // ca 3: cổng tương đối
      {{{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}}, -23.0},  // ca 4
      {{{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}}, -23.0},                                // ca 5
  };
  for (const uint32_t rate : {48000u, 44100u}) {
    for (std::size_t i = 0; i < std::size(cases); ++i) {
      SCOPED_TRACE(testing::Message() << rate << " Hz, ca " << i + 1);
      const LoudnessResult r = measureLoudness(sineSegments(rate, 2, cases[i].segments));
      ASSERT_TRUE(r.integratedLufs.has_value());
      EXPECT_NEAR(*r.integratedLufs, cases[i].want, 0.1);
    }
  }
}

// Mono được tính như mono kép (engine phát ra cả hai tai) → bằng đúng bản stereo cùng mức.
TEST(Loudness, MonoCountsAsDualMono) {
  const LoudnessResult mono = measureLoudness(sineSegments(48000, 1, {{-23.0, 10.0}}));
  ASSERT_TRUE(mono.integratedLufs.has_value());
  EXPECT_NEAR(*mono.integratedLufs, -23.0, 0.1);
  // > 2 kênh: chỉ 2 kênh đầu được phát nên chỉ đo 2 kênh đầu.
  const LoudnessResult quad = measureLoudness(sineSegments(48000, 4, {{-23.0, 10.0}}));
  ASSERT_TRUE(quad.integratedLufs.has_value());
  EXPECT_NEAR(*quad.integratedLufs, -23.0, 0.1);
}

TEST(Loudness, PeakAndSilence) {
  const LoudnessResult r = measureLoudness(sineSegments(48000, 2, {{-6.0, 2.0}}));
  EXPECT_NEAR(r.samplePeakDb, -6.0, 0.05);
  const LoudnessResult quiet = measureLoudness(sineSegments(48000, 2, {{-200.0, 5.0}}));
  EXPECT_FALSE(quiet.integratedLufs.has_value());
  EXPECT_LE(quiet.samplePeakDb, -199.0f);
  EXPECT_FALSE(measureLoudness(DecodedAudio{}).integratedLufs.has_value());
}
