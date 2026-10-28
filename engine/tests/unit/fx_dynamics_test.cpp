// Compressor và de-esser giọng (M5 B2): đường nén tĩnh, hằng thời gian, đi thẳng dưới ngưỡng;
// de-esser chỉ giảm băng "s", nguyên âm giữ nguyên.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "fx/compressor.h"
#include "fx/de_esser.h"
#include "fx/svf.h"
#include "util/fx_eval.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double sineAt(double hz, double db, std::size_t i) {
  return std::pow(10.0, db / 20.0) * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kFs);
}

// Nguyên âm tổng hợp: f0 220 Hz, hoạ âm tới 4 kHz giảm 6 dB / quãng tám, đỉnh ~ −20 dBFS.
double vowelAt(std::size_t i) {
  double s = 0.0;
  for (int h = 1; h * 220.0 <= 4000.0; ++h) s += sineAt(220.0 * h, -26.0 - 6.0 * std::log2(h), i);
  return s;
}

// Nhiễu "s": nhiễu trắng lọc thông dải 5,5–8,5 kHz (hai lần), RMS = `rms`.
std::vector<float> hiss(std::size_t n, double rms) {
  std::vector<float> x(n);
  uint32_t state = 11;
  for (float& v : x) {
    state = state * 1664525u + 1013904223u;
    v = static_cast<float>(static_cast<double>(state >> 8) / 8388608.0 - 1.0);
  }
  const SvfCoeffs bp = SvfCoeffs::make(SvfType::BandPass, kFs, 6850.0, 2.3, 0.0);
  for (int pass = 0; pass < 2; ++pass) {
    Svf f;
    for (float& v : x) v = f.process(v, bp);
  }
  double e = 0.0;
  for (const float v : x) e += static_cast<double>(v) * v;
  const double g = rms / std::sqrt(e / static_cast<double>(n));
  for (float& v : x) v = static_cast<float>(v * g);
  return x;
}

double energyDb(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double e = 0.0;
  for (std::size_t i = from; i < to; ++i) e += static_cast<double>(x[i]) * x[i];
  return 10.0 * std::log10(e + 1e-300);
}

// Phần thấp (< 2 kHz) / cao (> 5 kHz) của tín hiệu: lọc SVF bậc 4.
std::vector<float> band(const std::vector<float>& x, SvfType type, double hz) {
  std::vector<float> y = x;
  const SvfCoeffs c = SvfCoeffs::make(type, kFs, hz, 0.7071067811865476, 0.0);
  for (int pass = 0; pass < 2; ++pass) {
    Svf f;
    for (float& v : y) v = f.process(v, c);
  }
  return y;
}

}  // namespace

// Đường nén tĩnh đo bằng sin 1 kHz khớp công thức knee (±0,5 dB), kể cả makeup.
TEST(Compressor, StaticCurveMatchesKneeFormula) {
  const CompressorParams configs[] = {
      {-20.0f, 4.0f, 6.0f, 5.0f, 80.0f, 0.0f, 10.0f},
      {-30.0f, 2.0f, 0.0f, 5.0f, 80.0f, 0.0f, 10.0f},
      {-18.0f, 3.0f, 10.0f, 5.0f, 80.0f, 4.0f, 10.0f},
      {-24.0f, 6.0f, 6.0f, 5.0f, 80.0f, 0.0f, 10.0f},
  };
  std::vector<double> levels;
  for (double l = -60.0; l <= 0.0; l += 3.0) levels.push_back(l);
  for (const auto& p : configs) {
    Compressor comp(kFs);
    comp.setParams(p);
    comp.reset();
    const auto out = test::staticCurveDb([&](float* x, uint32_t n) { comp.process(x, n); }, levels,
                                         1000.0, kFs, 0.6);
    for (std::size_t i = 0; i < levels.size(); ++i) {
      const double want =
          levels[i] + Compressor::staticGainDb(levels[i], p) + static_cast<double>(p.makeupDb);
      EXPECT_NEAR(out[i], want, 0.5) << "T " << p.thresholdDb << " R " << p.ratio << " W "
                                     << p.kneeDb << " @ " << levels[i] << " dB";
    }
  }
}

// Bậc nhảy −40 → −10 dBFS (ngưỡng −30, 4:1 → giảm 15 dB) rồi về −40: mức giảm đạt 63% sau τ attack
// và còn 37% sau τ release (±20%).
TEST(Compressor, AttackAndReleaseTimeConstants) {
  CompressorParams p{-30.0f, 4.0f, 0.0f, 20.0f, 200.0f, 0.0f, 0.5f};
  Compressor comp(kFs);
  comp.setParams(p);
  const auto up = static_cast<std::size_t>(0.5 * kFs);
  const auto down = static_cast<std::size_t>(1.5 * kFs);
  const auto total = static_cast<std::size_t>(3.0 * kFs);
  std::vector<float> gr(total);
  for (std::size_t i = 0; i < total; ++i) {
    float v = static_cast<float>(sineAt(1000.0, i >= up && i < down ? -10.0 : -40.0, i));
    comp.process(&v, 1);
    gr[i] = comp.gainReductionDb();
  }
  EXPECT_NEAR(gr[down - 1], -15.0f, 0.3f);
  EXPECT_NEAR(gr[up + static_cast<std::size_t>(0.020 * kFs)], -15.0 * (1.0 - std::exp(-1.0)),
              0.2 * 15.0 * (1.0 - std::exp(-1.0)));
  EXPECT_NEAR(gr[down + static_cast<std::size_t>(0.200 * kFs)], -15.0 * std::exp(-1.0),
              0.2 * 15.0 * std::exp(-1.0));
  EXPECT_NEAR(gr[total - 1], 0.0f, 0.05f);
}

// Dưới ngưỡng (và knee) → hệ số đúng 1: ra = vào từng mẫu.
TEST(Compressor, TransparentBelowThreshold) {
  Compressor comp(kFs);
  comp.setParams({-18.0f, 3.0f, 6.0f, 5.0f, 80.0f, 0.0f, 10.0f});
  std::vector<float> x(48000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(sineAt(440.0, -30.0, i));
  std::vector<float> y = x;
  comp.process(y.data(), static_cast<uint32_t>(y.size()));
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_EQ(y[i], x[i]) << i;
}

// Đổi ngưỡng / makeup đột ngột khi đang phát: mức giảm trượt theo attack / release → không click
// (năng lượng trên 12 kHz ≤ −60 dB, đầu vào sin 300 Hz).
TEST(Compressor, ParameterJumpsDoNotClick) {
  Compressor comp(kFs);
  CompressorParams p{-10.0f, 4.0f, 6.0f, 5.0f, 80.0f, 0.0f, 10.0f};
  comp.setParams(p);
  std::vector<float> y(static_cast<std::size_t>(2.0 * kFs));
  for (std::size_t start = 0, k = 0; start < y.size(); start += 4800, ++k) {
    p.thresholdDb = k % 2 == 0 ? -30.0f : -10.0f;
    p.makeupDb = k % 2 == 0 ? 6.0f : 0.0f;
    comp.setParams(p);
    for (std::size_t i = start; i < start + 4800; ++i)
      y[i] = static_cast<float>(sineAt(300.0, -6.0, i));
    comp.process(y.data() + start, 4800);
  }
  EXPECT_LT(test::energyAboveDb(y, 12000.0, kFs), -60.0);
}

// Nguyên âm (không có "s") → de-esser không giảm gì: ra = vào từng mẫu.
TEST(DeEsser, VowelPassesBitExact) {
  DeEsser de(kFs);
  std::vector<float> x(48000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(vowelAt(i));
  std::vector<float> y = x;
  de.process(y.data(), static_cast<uint32_t>(y.size()));
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_EQ(y[i], x[i]) << i;
  EXPECT_EQ(de.gainReductionDb(), 0.0f);
}

// Nguyên âm + tiếng "s" (băng 5,5–8,5 kHz, mạnh hơn ngưỡng ~12 dB) từng đợt 150 ms: phần trên 5 kHz
// trong đợt "s" giảm ≥ 5 dB; phần dưới 2 kHz (thân nguyên âm) đổi ≤ 0,5 dB; giữa các đợt (sau khi
// nhả) đúng như vào.
TEST(DeEsser, ReducesSibilanceOnly) {
  DeEsser de(kFs);
  const std::size_t n = static_cast<std::size_t>(2.0 * kFs);
  const auto s = hiss(n, std::pow(10.0, (-30.0 + 12.0 - 3.01) / 20.0));
  std::vector<float> x(n);
  const auto burst = [](std::size_t i) {
    const double t = std::fmod(static_cast<double>(i) / kFs, 0.5);
    return t >= 0.1 && t < 0.25;
  };
  for (std::size_t i = 0; i < n; ++i)
    x[i] = static_cast<float>(vowelAt(i)) + (burst(i) ? s[i] : 0.0f);
  std::vector<float> y = x;
  de.process(y.data(), static_cast<uint32_t>(n));

  const auto hiIn = band(x, SvfType::HighPass, 5000.0);
  const auto hiOut = band(y, SvfType::HighPass, 5000.0);
  const auto loIn = band(x, SvfType::LowPass, 2000.0);
  const auto loOut = band(y, SvfType::LowPass, 2000.0);
  for (double start = 0.5; start < 2.0; start += 0.5) {
    const auto a = static_cast<std::size_t>((start + 0.12) * kFs);  // bỏ 20 ms đầu đợt (attack)
    const auto b = static_cast<std::size_t>((start + 0.25) * kFs);
    EXPECT_LT(energyDb(hiOut, a, b) - energyDb(hiIn, a, b), -5.0) << start;
    EXPECT_NEAR(energyDb(loOut, a, b), energyDb(loIn, a, b), 0.5) << start;
    // Giữa hai đợt, sau 5τ release (250 ms) + thời gian bộ dò RMS hạ xuống: đi thẳng.
    const auto c = static_cast<std::size_t>((start + 0.25 + 0.20) * kFs);
    const auto d = static_cast<std::size_t>((start + 0.5) * kFs);
    EXPECT_NEAR(energyDb(y, c, d), energyDb(x, c, d), 0.05) << start;
  }
}

// Sin 6,7 kHz (tâm băng dò) vượt ngưỡng 12 dB: 4:1 → giảm 9 dB nếu cho phép; mặc định chặn ở 6 dB.
TEST(DeEsser, RatioAndMaxReduction) {
  for (const float cap : {20.0f, 6.0f}) {
    DeEsser de(kFs);
    DeEsserParams p;
    p.maxReductionDb = cap;
    de.setParams(p);
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.5 * kFs); ++i) {
      float v = static_cast<float>(sineAt(6708.2, -18.0, i));
      de.process(&v, 1);
    }
    EXPECT_NEAR(de.gainReductionDb(), cap > 9.0f ? -9.0f : -6.0f, 0.3f) << cap;
  }
}

// Lựa chọn shelf mặc định: giảm hết thì băng "s" (6,7–9 kHz) mất ≥ 5,5 dB, còn vùng rõ chữ 3–4 kHz
// đổi ≤ 1 dB và dưới 3 kHz ≤ 0,5 dB (không làm "ngọng").
TEST(DeEsser, DefaultShelfSparesPresenceBand) {
  const DeEsserParams p;
  const SvfCoeffs c =
      SvfCoeffs::make(SvfType::HighShelf, kFs, static_cast<double>(p.shelfHz),
                      static_cast<double>(p.shelfQ), -static_cast<double>(p.maxReductionDb));
  const auto db = [&](double f) { return 20.0 * std::log10(c.magnitudeAt(f, kFs)); };
  for (const double f : {6700.0, 8000.0, 9000.0}) EXPECT_LT(db(f), -5.5) << f;
  for (const double f : {3000.0, 3500.0, 4000.0}) EXPECT_NEAR(db(f), 0.0, 1.0) << f;
  for (const double f : {200.0, 1000.0, 2000.0}) EXPECT_NEAR(db(f), 0.0, 0.5) << f;
}
