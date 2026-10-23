#include "key/hpss.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 12000.0;

double energy(const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0) {
  if (to == 0) to = x.size();
  double e = 0.0;
  for (std::size_t i = from; i < to; ++i) e += static_cast<double>(x[i]) * x[i];
  return e;
}

double errorDb(const std::vector<float>& got, const std::vector<float>& want, std::size_t from,
               std::size_t to) {
  double err = 0.0;
  for (std::size_t i = from; i < to; ++i) {
    const double d = static_cast<double>(got[i]) - want[i];
    err += d * d;
  }
  return 10.0 * std::log10(err / energy(want, from, to));
}

}  // namespace

// Mh + Mp = 1 → harmonic + percussive phải dựng lại đúng tín hiệu gốc (kể cả hai đầu).
TEST(Hpss, PartsSumToInput) {
  std::mt19937 rng(7);
  std::normal_distribution<float> g(0.0f, 0.2f);
  std::vector<float> x(static_cast<std::size_t>(kRate * 3) + 123);
  for (float& v : x) v = g(rng);
  test::addNote(x, kRate, 0.5, 2.0, 57, 0.3f);
  HpssOutput out;
  ASSERT_EQ(separateHarmonicPercussive(x, kRate, HpssConfig{}, nullptr, nullptr, out), TC_OK);
  ASSERT_EQ(out.harmonic.size(), x.size());
  std::vector<float> sum(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) sum[i] = out.harmonic[i] + out.percussive[i];
  EXPECT_LT(errorDb(sum, x, 0, x.size()), -90.0);
}

// Hợp âm ngân + tiếng gõ đều đặn: phần hoà âm ≈ hợp âm, phần bộ gõ ≈ tiếng gõ.
TEST(Hpss, SeparatesChordFromClicks) {
  const std::size_t n = static_cast<std::size_t>(kRate * 6);
  std::vector<float> tones(n, 0.0f);
  std::vector<float> clicks(n, 0.0f);
  for (const double m : {48.0, 52.0, 55.0, 60.0}) test::addNote(tones, kRate, 0.0, 6.0, m, 0.02f);
  for (int i = 0; i < 12; ++i) test::addClick(clicks, kRate, 0.25 + 0.5 * i, 1.5f, 100 + i);
  std::vector<float> mix(n);
  for (std::size_t i = 0; i < n; ++i) mix[i] = tones[i] + clicks[i];

  HpssOutput out;
  ASSERT_EQ(separateHarmonicPercussive(mix, kRate, HpssConfig{}, nullptr, nullptr, out), TC_OK);
  // Trước khi tách tiếng gõ to hơn hợp âm ~3 dB; đo được (12 kHz): harmonic −12,9 dB,
  // percussive −16,1 dB. Bỏ 0,5 s hai đầu (attack/release của nốt).
  const auto from = static_cast<std::size_t>(0.5 * kRate);
  const auto to = n - from;
  const double inputDb = errorDb(mix, tones, from, to);  // trước khi tách: tiếng gõ là "lỗi"
  const double harmDb = errorDb(out.harmonic, tones, from, to);
  const double percDb = errorDb(out.percussive, clicks, from, to);
  EXPECT_GT(inputDb, 0.0) << "tiếng gõ phải to hơn hợp âm để test có ý nghĩa";
  EXPECT_LT(harmDb, -10.0) << "input " << inputDb;
  EXPECT_LT(percDb, -12.0) << "input " << inputDb;
}

TEST(Hpss, HarmonicFrameCallback) {
  std::vector<float> x(static_cast<std::size_t>(kRate * 2), 0.0f);
  test::addNote(x, kRate, 0.0, 2.0, 69, 0.3f, {1, 0.0, 0.0});
  const HpssConfig cfg;
  std::size_t frames = 0;
  double lastTime = -1.0;
  bool increasing = true;
  float peakHz = 0.0f;
  HpssOutput out;
  ASSERT_EQ(separateHarmonicPercussive(
                x, kRate, cfg,
                [&](const float* mag, std::size_t bins, double t) {
                  EXPECT_EQ(bins, cfg.fftSize / 2 + 1);
                  increasing = increasing && t > lastTime;
                  lastTime = t;
                  if (frames++ == 40) {
                    std::size_t best = 0;
                    for (std::size_t k = 1; k < bins; ++k) {
                      if (mag[k] > mag[best]) best = k;
                    }
                    peakHz = static_cast<float>(static_cast<double>(best) * kRate /
                                                static_cast<double>(cfg.fftSize));
                  }
                },
                nullptr, out),
            TC_OK);
  EXPECT_TRUE(increasing);
  EXPECT_NEAR(static_cast<double>(frames),
              static_cast<double>(x.size()) / static_cast<double>(cfg.hop), 4.0);
  EXPECT_NEAR(peakHz, 440.0f, kRate / cfg.fftSize);
}

// Đầu vào có NaN/inf không được làm hỏng bộ nhớ (ASan) hay treo.
TEST(Hpss, NonFiniteInputIsSafe) {
  std::vector<float> x(static_cast<std::size_t>(kRate), 0.1f);
  x[5000] = std::numeric_limits<float>::quiet_NaN();
  x[7000] = std::numeric_limits<float>::infinity();
  HpssOutput out;
  EXPECT_EQ(separateHarmonicPercussive(x, kRate, HpssConfig{}, nullptr, nullptr, out), TC_OK);
  EXPECT_EQ(out.harmonic.size(), x.size());
}

TEST(Hpss, CancelAndInvalidConfig) {
  std::vector<float> x(static_cast<std::size_t>(kRate * 10), 0.1f);
  HpssOutput out;
  int calls = 0;
  EXPECT_EQ(separateHarmonicPercussive(
                x, kRate, HpssConfig{}, nullptr, [&](float) { return ++calls < 2; }, out),
            kResultCancelled);
  EXPECT_TRUE(out.harmonic.empty());

  HpssConfig bad;
  bad.timeMedian = 16;  // phải lẻ
  EXPECT_EQ(separateHarmonicPercussive(x, kRate, bad, nullptr, nullptr, out), TC_ERR_INVALID_ARG);
  bad = HpssConfig{};
  bad.hop = 300;  // không chia hết fftSize
  EXPECT_EQ(separateHarmonicPercussive(x, kRate, bad, nullptr, nullptr, out), TC_ERR_INVALID_ARG);
}
