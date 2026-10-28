// EQ giọng (M5 B1): SVF dạng TPT khớp đáp ứng RBJ cookbook, EQ nhiều tầng, đổi thông số liên tục
// không nổ / không "lụp bụp".

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "dsp/biquad.h"
#include "fx/equalizer.h"
#include "fx/svf.h"
#include "util/fx_eval.h"

using namespace tonecore;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Case {
  SvfType type;
  double f0;
  double q;
  double db;
};

BiquadCoeffs rbj(const Case& c, double fs) {
  switch (c.type) {
    case SvfType::LowPass:
      return BiquadCoeffs::lowpass(fs, c.f0, c.q);
    case SvfType::HighPass:
      return BiquadCoeffs::highpass(fs, c.f0, c.q);
    case SvfType::Bell:
      return BiquadCoeffs::peaking(fs, c.f0, c.q, c.db);
    case SvfType::LowShelf:
      return BiquadCoeffs::lowShelf(fs, c.f0, c.q, c.db);
    case SvfType::HighShelf:
      return BiquadCoeffs::highShelf(fs, c.f0, c.q, c.db);
    case SvfType::BandPass:
      return BiquadCoeffs::bandpass(fs, c.f0, c.q);
    default:
      return BiquadCoeffs{};
  }
}

const std::vector<Case>& cases() {
  static const std::vector<Case> c = {
      {SvfType::HighPass, 80.0, 0.7071, 0.0},     {SvfType::HighPass, 150.0, 1.2, 0.0},
      {SvfType::LowPass, 9000.0, 0.7071, 0.0},    {SvfType::LowPass, 2500.0, 0.5, 0.0},
      {SvfType::Bell, 300.0, 1.0, -2.0},          {SvfType::Bell, 3000.0, 1.0, 2.0},
      {SvfType::Bell, 5000.0, 4.0, 12.0},         {SvfType::Bell, 120.0, 0.3, -12.0},
      {SvfType::LowShelf, 200.0, 0.7071, 6.0},    {SvfType::LowShelf, 500.0, 1.0, -9.0},
      {SvfType::HighShelf, 12000.0, 0.7071, 2.0}, {SvfType::HighShelf, 4000.0, 0.5, -6.0},
      {SvfType::BandPass, 6700.0, 1.7, 0.0},      {SvfType::BandPass, 1000.0, 0.7, 0.0},
  };
  return c;
}

}  // namespace

// Công thức đáp ứng của SVF (s = j·tan(πf/fs)/g thay vào nguyên mẫu) trùng RBJ cookbook — đúng biến
// đổi song tuyến tính có prewarp — và đo trên đáp ứng xung thật của bộ lọc float khớp cả hai.
TEST(Svf, MatchesRbjCookbook) {
  for (const double fs : {44100.0, 48000.0}) {
    for (const auto& c : cases()) {
      const SvfCoeffs svf = SvfCoeffs::make(c.type, fs, c.f0, c.q, c.db);
      const BiquadCoeffs ref = rbj(c, fs);
      Svf filt;
      const auto ir = test::impulseResponse(
          [&](float* x, uint32_t n) {
            for (uint32_t i = 0; i < n; ++i) x[i] = filt.process(x[i], svf);
          },
          static_cast<std::size_t>(fs));  // 1 s: đủ dài cho đáp ứng ở 20 Hz
      for (const double f : test::logFrequencies()) {
        const double want = 20.0 * std::log10(ref.magnitudeAt(f, fs));
        EXPECT_NEAR(20.0 * std::log10(svf.magnitudeAt(f, fs)), want, 0.01)
            << static_cast<int>(c.type) << " f0 " << c.f0 << " @ " << f << " Hz";
        EXPECT_NEAR(test::magnitudeDb(ir, f, fs), want, 0.1)
            << static_cast<int>(c.type) << " f0 " << c.f0 << " đo @ " << f << " Hz";
      }
    }
  }
}

// Xử lý cả khối (biến cục bộ) cho ra đúng từng mẫu như xử lý từng mẫu.
TEST(Svf, BlockMatchesPerSample) {
  const SvfCoeffs c = SvfCoeffs::make(SvfType::Bell, 48000.0, 2500.0, 1.5, 6.0);
  std::vector<float> a(1000);
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)) + 0.1 * (i % 7 == 0));
  }
  std::vector<float> b = a;
  Svf s1, s2;
  for (float& v : a) v = s1.process(v, c);
  s2.processBlock(b.data(), 333, c);
  s2.processBlock(b.data() + 333, 667, c);
  for (std::size_t i = 0; i < a.size(); ++i) ASSERT_EQ(a[i], b[i]) << i;
}

// HPF giọng 80 Hz (CLAUDE.md 3.2): −3 dB tại 80 Hz, bậc 2 → ~ −24 dB ở 20 Hz, phẳng ở giọng nói.
TEST(Equalizer, VoiceHighPass80Hz) {
  Equalizer eq(48000.0);
  EqParams p;
  p.highPassHz = 80.0f;
  eq.setParamsImmediate(p);
  const auto ir = test::impulseResponse([&](float* x, uint32_t n) { eq.process(x, n); }, 48000);
  EXPECT_NEAR(test::magnitudeDb(ir, 80.0, 48000.0), -3.01, 0.1);
  EXPECT_LT(test::magnitudeDb(ir, 20.0, 48000.0), -23.5);
  EXPECT_NEAR(test::magnitudeDb(ir, 300.0, 48000.0), 0.0, 0.1);
  EXPECT_NEAR(test::magnitudeDb(ir, 3000.0, 48000.0), 0.0, 0.01);
}

// Nhiều tầng nối tiếp: đáp ứng đo = tích đáp ứng các tầng (preset Bolero + thông thấp như Lofi).
TEST(Equalizer, CascadeMatchesProductOfBands) {
  Equalizer eq(48000.0);
  EqParams p;
  p.highPassHz = 80.0f;
  p.lowPassHz = 9000.0f;
  p.bands[0] = {SvfType::Bell, 300.0f, -2.0f, 1.0f};
  p.bands[1] = {SvfType::Bell, 3000.0f, 2.0f, 1.0f};
  p.bands[2] = {SvfType::HighShelf, 12000.0f, 2.0f, 0.7071f};
  p.bands[3] = {SvfType::LowShelf, 150.0f, -3.0f, 0.7071f};
  eq.setParamsImmediate(p);
  const auto ir = test::impulseResponse([&](float* x, uint32_t n) { eq.process(x, n); }, 48000);
  for (const double f : test::logFrequencies()) {
    double want = 1.0;
    want *= BiquadCoeffs::highpass(48000.0, 80.0, 0.70710678118654752).magnitudeAt(f, 48000.0);
    want *= BiquadCoeffs::lowpass(48000.0, 9000.0, 0.70710678118654752).magnitudeAt(f, 48000.0);
    want *= BiquadCoeffs::peaking(48000.0, 300.0, 1.0, -2.0).magnitudeAt(f, 48000.0);
    want *= BiquadCoeffs::peaking(48000.0, 3000.0, 1.0, 2.0).magnitudeAt(f, 48000.0);
    want *= BiquadCoeffs::highShelf(48000.0, 12000.0, 0.7071, 2.0).magnitudeAt(f, 48000.0);
    want *= BiquadCoeffs::lowShelf(48000.0, 150.0, 0.7071, -3.0).magnitudeAt(f, 48000.0);
    EXPECT_NEAR(test::magnitudeDb(ir, f, 48000.0), 20.0 * std::log10(want), 0.1) << f << " Hz";
    EXPECT_NEAR(20.0 * std::log10(eq.magnitudeAt(f)), 20.0 * std::log10(want), 0.01) << f;
  }
}

// Mọi băng tắt (hoặc 0 dB) → đi thẳng, đúng từng mẫu.
TEST(Equalizer, FlatIsBitExact) {
  Equalizer eq(48000.0);
  EqParams p;
  p.bands[1] = {SvfType::Bell, 2000.0f, 0.0f, 1.0f};
  eq.setParamsImmediate(p);
  std::vector<float> x(4800);
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = static_cast<float>(0.5 * std::sin(0.01 * static_cast<double>(i)));
  }
  std::vector<float> y = x;
  eq.process(y.data(), static_cast<uint32_t>(y.size()));
  for (std::size_t i = 0; i < x.size(); ++i) ASSERT_EQ(y[i], x[i]) << i;
}

// Kéo thanh trượt liên tục (cứ 10 ms đặt thông số mới): tần số 200 Hz → 8 kHz và −12 → +12 dB
// trong 2 s, đầu vào sin 300 Hz: không nổ (luôn hữu hạn, biên độ hợp lý), không "lụp bụp" (năng
// lượng trên 12 kHz ≤ −60 dB so với tổng).
TEST(Equalizer, ContinuousSweepIsSmoothAndStable) {
  constexpr double kFs = 48000.0;
  const auto run = [&]() {
    Equalizer eq(kFs);
    std::vector<float> y(static_cast<std::size_t>(2.0 * kFs));
    constexpr uint32_t kStep = 480;  // 10 ms
    for (std::size_t start = 0; start < y.size(); start += kStep) {
      const double u = static_cast<double>(start) / static_cast<double>(y.size());
      EqParams p;
      p.bands[0] = {SvfType::Bell, static_cast<float>(200.0 * std::pow(40.0, u)),
                    static_cast<float>(-12.0 + 24.0 * u), 2.0f};
      p.bands[1] = {SvfType::HighShelf, static_cast<float>(8000.0 / std::pow(4.0, u)),
                    static_cast<float>(6.0 - 12.0 * u), 0.7071f};
      eq.setParams(p);
      for (std::size_t i = start; i < start + kStep; ++i) {
        y[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 300.0 * static_cast<double>(i) / kFs));
      }
      eq.process(y.data() + start, kStep);
    }
    return y;
  };
  const auto y = run();
  float peak = 0.0f;
  for (const float v : y) {
    ASSERT_TRUE(std::isfinite(v));
    peak = std::max(peak, std::fabs(v));
  }
  EXPECT_LT(peak, 0.3f * 4.5f);  // +12 dB tối đa (×4) + vồng nhỏ
  EXPECT_LT(test::energyAboveDb(y, 12000.0, kFs), -60.0);
}

// Thông số nhảy mạnh qua lại mỗi 10 ms (200 Hz −12 dB ↔ 8 kHz +12 dB): làm mượt 20 ms khử tiếng
// "lụp bụp" (≤ −60 dB trên 12 kHz); không làm mượt thì nghe rõ (> −50 dB) — phép đo đủ nhạy.
TEST(Equalizer, SmoothingRemovesClicksOnJumps) {
  constexpr double kFs = 48000.0;
  const auto run = [&](double smoothSec) {
    Equalizer eq(kFs, smoothSec);
    std::vector<float> y(static_cast<std::size_t>(2.0 * kFs));
    constexpr uint32_t kStep = 480;
    for (std::size_t start = 0, k = 0; start < y.size(); start += kStep, ++k) {
      const bool hi = k % 2 == 0;
      EqParams p;
      p.bands[0] = {SvfType::Bell, hi ? 8000.0f : 200.0f, hi ? 12.0f : -12.0f, 2.0f};
      eq.setParams(p);
      for (std::size_t i = start; i < start + kStep; ++i) {
        y[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 300.0 * static_cast<double>(i) / kFs));
      }
      eq.process(y.data() + start, kStep);
    }
    return test::energyAboveDb(y, 12000.0, kFs);
  };
  EXPECT_LT(run(0.02), -60.0);
  EXPECT_GT(run(0.0), -50.0);
}
