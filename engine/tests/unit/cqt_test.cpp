#include "key/cqt.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "util/synth_music.h"

using namespace tonecore;

namespace {

std::vector<float> sine(double rate, double secs, double hz, float amp) {
  std::vector<float> x(static_cast<std::size_t>(rate * secs));
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = amp * static_cast<float>(
                     std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / rate));
  }
  return x;
}

double db(double v) {
  return 10.0 * std::log10(v + 1e-30);
}

int argmax(const float* p, int n) {
  int best = 0;
  for (int k = 1; k < n; ++k) {
    if (p[k] > p[best]) best = k;
  }
  return best;
}

}  // namespace

class CqtRate : public ::testing::TestWithParam<double> {};

// Sin đúng tâm nốt MIDI m → đỉnh ở bin 3(m − 28) + 1, công suất A²/4 (±0,5 dB) ở MỌI quãng 8
// (kiểm tra chuỗi hạ mẫu ÷2 và chuẩn hoá kernel), nốt kề (±1 nửa cung) thấp hơn ≥ 25 dB.
TEST_P(CqtRate, SinePeaksAtNoteBinWithFlatGain) {
  const double rate = GetParam();
  const float amp = 0.5f;
  for (const int m : {29, 40, 52, 64, 69, 76, 88, 98}) {
    SCOPED_TRACE(m);
    const auto x = sine(rate, 8.0, test::midiToHz(m), amp);
    CqtSpectrogram s;
    ASSERT_EQ(computeCqt(x, rate, CqtConfig{}, nullptr, s), TC_OK);
    ASSERT_EQ(s.bins, 216);
    const float* p = s.frame(s.frames / 2);
    const int k = argmax(p, s.bins);
    EXPECT_EQ(k, 3 * (m - 28) + 1);
    EXPECT_NEAR(db(p[k]), db(amp * amp / 4.0), 0.5);
    for (const int nb : {k - 3, k + 3}) {
      if (nb >= 0 && nb < s.bins) EXPECT_LT(db(p[nb]) - db(p[k]), -25.0) << nb;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Rates, CqtRate, ::testing::Values(12000.0, 11025.0, 16000.0));

// tuningCents dời lưới bin: sin lệch +40 cent rơi đúng tâm nốt khi CQT biết tuning, lệch 1 bin
// (1/3 nửa cung) khi không biết.
TEST(Cqt, TuningShiftsGrid) {
  const double rate = 12000.0;
  const auto x = sine(rate, 6.0, test::midiToHz(57, 40.0), 0.5f);
  CqtConfig cfg;
  CqtSpectrogram plain;
  ASSERT_EQ(computeCqt(x, rate, cfg, nullptr, plain), TC_OK);
  EXPECT_EQ(argmax(plain.frame(plain.frames / 2), plain.bins), 3 * (57 - 28) + 2);
  cfg.tuningCents = 40.0f;
  CqtSpectrogram tuned;
  ASSERT_EQ(computeCqt(x, rate, cfg, nullptr, tuned), TC_OK);
  const int k = argmax(tuned.frame(tuned.frames / 2), tuned.bins);
  EXPECT_EQ(k, 3 * (57 - 28) + 1);
  EXPECT_NEAR(tuned.binMidi(k), 57.4, 1e-9);
}

// Nốt bắt đầu ở giây thứ 5: khung đạt −6 dB mức ổn định phải ở 5 s ± 1 khung, cho cả nốt trầm
// (quãng 8 hạ mẫu 32 lần) lẫn nốt cao — kiểm tra bù trễ bộ lọc hạ mẫu.
TEST(Cqt, OnsetTimeAlignedAcrossOctaves) {
  const double rate = 12000.0;
  for (const int m : {33, 93}) {
    SCOPED_TRACE(m);
    std::vector<float> x(static_cast<std::size_t>(rate * 12), 0.0f);
    test::addNote(x, rate, 5.0, 7.0, m, 0.5f, {1, 0.0, 0.0});
    CqtSpectrogram s;
    ASSERT_EQ(computeCqt(x, rate, CqtConfig{}, nullptr, s), TC_OK);
    const int k = 3 * (m - 28) + 1;
    const float steady = s.frame(static_cast<std::size_t>(9.0 / s.frameSec))[k];
    std::size_t first = 0;
    while (first < s.frames && s.frame(first)[k] < 0.25f * steady) ++first;
    EXPECT_NEAR(static_cast<double>(first) * s.frameSec, 5.0, s.frameSec);
  }
}

TEST(Cqt, FrameRateAndInvalidInput) {
  const auto x = sine(12000.0, 10.0, 440.0, 0.5f);
  CqtSpectrogram s;
  ASSERT_EQ(computeCqt(x, 12000.0, CqtConfig{}, nullptr, s), TC_OK);
  EXPECT_NEAR(s.frameSec, 128.0 * 32.0 / 12000.0, 1e-12);
  EXPECT_EQ(s.frames, (x.size() - 1) / (128 * 32) + 1);
  // 4 kHz: bin cao nhất (2,5 kHz) vượt 0,45 × tần số lấy mẫu.
  EXPECT_EQ(computeCqt(x, 4000.0, CqtConfig{}, nullptr, s), TC_ERR_INVALID_ARG);
  int calls = 0;
  EXPECT_EQ(
      computeCqt(x, 12000.0, CqtConfig{}, [&](float) { return ++calls < 2; }, s), kResultCancelled);
}
