#include "key/tempo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 12000.0;

// Mẫu nhịp: tiếng gõ mạnh ở mỗi phách, nhẹ ở nửa phách (kiểu trống + hi-hat móc đơn).
std::vector<float> beatPattern(double bpm, double sec, uint32_t seed) {
  std::vector<float> x(static_cast<std::size_t>(kRate * sec), 0.0f);
  const double beat = 60.0 / bpm;
  for (int i = 0; i * beat < sec; ++i) {
    test::addClick(x, kRate, i * beat, 0.8f, seed + 2 * static_cast<uint32_t>(i));
    test::addClick(x, kRate, (i + 0.5) * beat, 0.3f, seed + 2 * static_cast<uint32_t>(i) + 1);
  }
  return x;
}

std::optional<TempoEstimate> tempoOf(const std::vector<float>& x) {
  std::optional<TempoEstimate> est;
  EXPECT_EQ(estimateTempo(x, kRate, TempoConfig{}, nullptr, est), TC_OK);
  return est;
}

}  // namespace

// Tempo gần prior (90–140 BPM): đúng quãng 8, sai ≤ 1%.
TEST(Tempo, CommonTemposWithinOnePercent) {
  for (const double bpm : {90.0, 100.0, 120.0, 128.0, 140.0}) {
    SCOPED_TRACE(bpm);
    const auto est = tempoOf(beatPattern(bpm, 30.0, static_cast<uint32_t>(bpm)));
    ASSERT_TRUE(est.has_value());
    EXPECT_NEAR(est->bpm, bpm, 0.01 * bpm);
    EXPECT_GT(est->confidence, 0.4f);
  }
}

// Tempo xa prior: chấp nhận nhầm quãng 8 (×2, ×½) như chỉ số Acc2, sai ≤ 2%.
TEST(Tempo, FarTemposWithinOctave) {
  for (const double bpm : {60.0, 72.0, 160.0, 180.0}) {
    SCOPED_TRACE(bpm);
    const auto est = tempoOf(beatPattern(bpm, 30.0, static_cast<uint32_t>(bpm)));
    ASSERT_TRUE(est.has_value());
    bool ok = false;
    for (const double f : {1.0, 2.0, 0.5})
      ok = ok || std::fabs(est->bpm - f * bpm) <= 0.02 * f * bpm;
    EXPECT_TRUE(ok) << est->bpm;
  }
}

// Nhiễu không có chu kỳ → độ tin cậy thấp; quá ngắn / im lặng → không kết luận.
TEST(Tempo, NoiseShortAndSilence) {
  std::mt19937 rng(3);
  std::vector<float> noise(static_cast<std::size_t>(kRate * 20.0), 0.0f);
  std::uniform_real_distribution<double> when(0.0, 20.0);
  for (int i = 0; i < 60; ++i) test::addClick(noise, kRate, when(rng), 0.6f, 900 + i);
  const auto n = tempoOf(noise);
  if (n) EXPECT_LT(n->confidence, 0.3f) << n->bpm;
  EXPECT_FALSE(tempoOf(beatPattern(120.0, 3.0, 1)).has_value());
  EXPECT_FALSE(
      tempoOf(std::vector<float>(static_cast<std::size_t>(kRate * 10.0), 0.0f)).has_value());
}

TEST(Tempo, OnsetEnvelopePeaksAtClicks) {
  std::vector<float> x(static_cast<std::size_t>(kRate * 4.0), 0.0f);
  test::addClick(x, kRate, 2.0, 0.8f, 5);
  const TempoConfig cfg;
  const std::vector<float> env = onsetEnvelope(x, kRate, cfg);
  ASSERT_FALSE(env.empty());
  const auto peak =
      static_cast<std::size_t>(std::max_element(env.begin(), env.end()) - env.begin());
  // Tâm khung j ở (j·hop + fftSize/2) / rate.
  const double t = (static_cast<double>(peak * cfg.hop) + 0.5 * cfg.fftSize) / kRate;
  EXPECT_NEAR(t, 2.0, 0.03);
}
