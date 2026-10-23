#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "key/chroma.h"
#include "key/cqt.h"
#include "key/hpss.h"
#include "key/tuning.h"
#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 12000.0;

// Vòng hợp âm C – F – G – Am (2 s mỗi hợp âm) kèm nốt bass, lặp `loops` lần.
std::vector<float> progression(double tuningCents, int loops, double decay = 0.0,
                               bool clicks = false) {
  const int chords[4][4] = {{48, 60, 64, 67}, {41, 60, 65, 69}, {43, 62, 67, 71}, {45, 60, 64, 69}};
  std::vector<float> x(static_cast<std::size_t>(kRate * 8.0 * loops), 0.0f);
  test::NoteSpec spec;
  spec.tuningCents = tuningCents;
  spec.decayPerSec = decay;
  for (int l = 0; l < loops; ++l) {
    for (int c = 0; c < 4; ++c) {
      const double t = 8.0 * l + 2.0 * c;
      for (const int m : chords[c]) test::addNote(x, kRate, t, 2.0, m, 0.08f, spec);
    }
  }
  if (clicks) {
    for (int i = 0; i < 16 * loops; ++i) test::addClick(x, kRate, 0.5 * i, 0.5f, 1000 + i);
  }
  return x;
}

std::optional<float> estimateTuning(const std::vector<float>& x) {
  const HpssConfig cfg;
  TuningEstimator est(kRate, cfg.fftSize);
  HpssOutput out;
  EXPECT_EQ(
      separateHarmonicPercussive(
          x, kRate, cfg, [&](const float* m, std::size_t bins, double) { est.addFrame(m, bins); },
          nullptr, out),
      TC_OK);
  return est.estimate();
}

double circularDiff(double a, double b) {
  double d = std::fmod(a - b + 150.0, 100.0);
  if (d < 0) d += 100.0;
  return d - 50.0;
}

std::vector<int> topPitchClasses(const ChromaVector& v, int n) {
  std::vector<int> idx(12);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    return v[static_cast<std::size_t>(a)] > v[static_cast<std::size_t>(b)];
  });
  idx.resize(static_cast<std::size_t>(n));
  std::sort(idx.begin(), idx.end());
  return idx;
}

}  // namespace

class TuningOffset : public ::testing::TestWithParam<double> {};

TEST_P(TuningOffset, EstimatesWithinThreeCents) {
  const double cents = GetParam();
  const auto est = estimateTuning(progression(cents, 2));
  ASSERT_TRUE(est.has_value());
  EXPECT_LE(std::fabs(circularDiff(*est, cents)), 3.0) << *est;
}

INSTANTIATE_TEST_SUITE_P(Cents, TuningOffset,
                         ::testing::Values(0.0, 30.0, -45.0, 49.0, -20.0, 12.0));

// Nốt tắt dần kiểu piano + trống to: vẫn trong ±3 cent.
TEST(Tuning, RobustToDecayAndDrums) {
  const auto est = estimateTuning(progression(-27.0, 2, 1.5, true));
  ASSERT_TRUE(est.has_value());
  EXPECT_LE(std::fabs(circularDiff(*est, -27.0)), 3.0) << *est;
}

TEST(Tuning, SilenceHasNoEstimate) {
  EXPECT_FALSE(estimateTuning(std::vector<float>(static_cast<std::size_t>(kRate * 4), 0.0f)));
}

// Hợp âm Đô trưởng (bass C3) → treble nổi C, E, G; bass nổi C. La thứ (bass A2) → bass nổi A.
// Beat lệch +40 cent, CQT dời theo tuning → vẫn đúng.
TEST(Chroma, TriadsAndBass) {
  struct Case {
    std::vector<int> notes;
    std::vector<int> treble;
    int bass;
  };
  const Case cases[] = {{{48, 60, 64, 67, 72}, {0, 4, 7}, 0}, {{45, 57, 60, 64, 69}, {0, 4, 9}, 9}};
  for (const double tuning : {0.0, 40.0}) {
    for (const Case& c : cases) {
      SCOPED_TRACE(tuning);
      std::vector<float> x(static_cast<std::size_t>(kRate * 6), 0.0f);
      test::NoteSpec spec;
      spec.tuningCents = tuning;
      for (const int m : c.notes) test::addNote(x, kRate, 0.0, 6.0, m, 0.1f, spec);
      CqtConfig cfg;
      cfg.tuningCents = static_cast<float>(tuning);
      CqtSpectrogram s;
      ASSERT_EQ(computeCqt(x, kRate, cfg, nullptr, s), TC_OK);
      const Chromagram ch = computeChroma(s);
      ASSERT_EQ(ch.frames(), s.frames);
      const std::size_t mid = ch.frames() / 2;
      EXPECT_EQ(topPitchClasses(ch.treble[mid], 3), c.treble);
      EXPECT_EQ(topPitchClasses(ch.bass[mid], 1), std::vector<int>{c.bass});
    }
  }
}

TEST(Chroma, SilentFramesHaveLowEnergy) {
  std::vector<float> x(static_cast<std::size_t>(kRate * 10), 0.0f);
  test::addNote(x, kRate, 0.0, 4.0, 60, 0.2f);
  CqtSpectrogram s;
  ASSERT_EQ(computeCqt(x, kRate, CqtConfig{}, nullptr, s), TC_OK);
  const Chromagram ch = computeChroma(s);
  const float loud = ch.energyDb[static_cast<std::size_t>(2.0 / ch.frameSec)];
  const float quiet = ch.energyDb[static_cast<std::size_t>(8.0 / ch.frameSec)];
  EXPECT_GT(loud - quiet, 60.0f);
  for (const float v : ch.treble[static_cast<std::size_t>(8.0 / ch.frameSec)]) EXPECT_LT(v, 0.01f);
}
