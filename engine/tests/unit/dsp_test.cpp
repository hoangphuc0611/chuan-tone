// Test các khối DSP nền tảng của M2: FFT số thực, biquad, bộ hạ mẫu.

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "dsp/biquad.h"
#include "dsp/decimator.h"
#include "dsp/fft.h"

using tonecore::Biquad;
using tonecore::BiquadCoeffs;
using tonecore::Decimator;
using tonecore::RealFft;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<std::complex<double>> naiveDft(const std::vector<float>& x) {
  const std::size_t n = x.size();
  std::vector<std::complex<double>> out(n / 2 + 1);
  for (std::size_t k = 0; k <= n / 2; ++k) {
    std::complex<double> acc = 0.0;
    for (std::size_t t = 0; t < n; ++t) {
      acc += static_cast<double>(x[t]) *
             std::polar(1.0, -2.0 * kPi * static_cast<double>(k * t) / static_cast<double>(n));
    }
    out[k] = acc;
  }
  return out;
}

std::vector<float> randomSignal(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<float> v(n);
  for (auto& s : v) s = dist(rng);
  return v;
}

// Biên độ của thành phần tần số f trong x (khớp sin/cos bình phương tối thiểu trên cả đoạn).
double toneAmplitude(const std::vector<float>& x, double f, double rate, std::size_t skip = 0) {
  double c = 0.0, s = 0.0;
  std::size_t count = 0;
  for (std::size_t i = skip; i < x.size(); ++i) {
    const double ph = 2.0 * kPi * f * static_cast<double>(i) / rate;
    c += x[i] * std::cos(ph);
    s += x[i] * std::sin(ph);
    ++count;
  }
  return 2.0 * std::hypot(c, s) / static_cast<double>(count);
}

}  // namespace

TEST(RealFft, RejectsBadSizes) {
  EXPECT_THROW(RealFft(0), std::invalid_argument);
  EXPECT_THROW(RealFft(2), std::invalid_argument);
  EXPECT_THROW(RealFft(48), std::invalid_argument);
}

TEST(RealFft, MatchesNaiveDft) {
  for (std::size_t n : {4u, 8u, 16u, 64u, 256u, 1024u}) {
    RealFft fft(n);
    const auto x = randomSignal(n, static_cast<unsigned>(n));
    std::vector<std::complex<float>> X(fft.bins());
    fft.forward(x.data(), X.data());
    const auto ref = naiveDft(x);
    double maxErr = 0.0;
    for (std::size_t k = 0; k < ref.size(); ++k) {
      maxErr = std::max(maxErr, std::abs(std::complex<double>(X[k]) - ref[k]));
    }
    // Sai số tương đối so với độ lớn điển hình sqrt(N).
    EXPECT_LT(maxErr / std::sqrt(static_cast<double>(n)), 1e-5) << "N=" << n;
  }
}

TEST(RealFft, InverseRoundTrip) {
  for (std::size_t n : {4u, 32u, 1024u, 4096u}) {
    RealFft fft(n);
    const auto x = randomSignal(n, 7u + static_cast<unsigned>(n));
    std::vector<std::complex<float>> X(fft.bins());
    std::vector<float> y(n);
    fft.forward(x.data(), X.data());
    fft.inverse(X.data(), y.data());
    double maxErr = 0.0;
    for (std::size_t i = 0; i < n; ++i) maxErr = std::max(maxErr, std::fabs(double(x[i] - y[i])));
    EXPECT_LT(maxErr, 1e-5) << "N=" << n;
  }
}

TEST(RealFft, SinePeaksAtItsBin) {
  const std::size_t n = 1024;
  RealFft fft(n);
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    x[i] = static_cast<float>(std::cos(2.0 * kPi * 37.0 * static_cast<double>(i) / n));
  }
  std::vector<std::complex<float>> X(fft.bins());
  fft.forward(x.data(), X.data());
  EXPECT_NEAR(std::abs(X[37]), n / 2.0, 1e-2);
  EXPECT_LT(std::abs(X[36]), 1e-2);
  EXPECT_LT(std::abs(X[38]), 1e-2);
}

TEST(Biquad, HighpassResponse) {
  const auto c = BiquadCoeffs::highpass(48000.0, 80.0);
  EXPECT_NEAR(20.0 * std::log10(c.magnitudeAt(80.0, 48000.0)), -3.01, 0.05);
  EXPECT_LT(20.0 * std::log10(c.magnitudeAt(20.0, 48000.0)), -23.0);
  EXPECT_NEAR(c.magnitudeAt(1000.0, 48000.0), 1.0, 0.01);

  // Lọc thật một sin 20 Hz và 1 kHz, so với đáp ứng lý thuyết.
  for (double f : {20.0, 1000.0}) {
    Biquad bq;
    bq.setCoeffs(c);
    std::vector<float> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i) {
      x[i] = static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(i) / 48000.0));
    }
    bq.process(x.data(), x.data(), x.size());
    EXPECT_NEAR(toneAmplitude(x, f, 48000.0, 24000), c.magnitudeAt(f, 48000.0), 0.01) << f;
  }
}

TEST(Biquad, LowpassResponse) {
  const auto c = BiquadCoeffs::lowpass(250.0, 2.0);
  EXPECT_NEAR(c.magnitudeAt(0.0, 250.0), 1.0, 1e-9);
  EXPECT_NEAR(20.0 * std::log10(c.magnitudeAt(2.0, 250.0)), -3.01, 0.05);
  EXPECT_LT(c.magnitudeAt(5.5, 250.0), 0.15);
}

TEST(Decimator, PassbandAndDelay) {
  Decimator d(3);
  EXPECT_NEAR(d.groupDelayInput(), 47.5, 1e-12);
  const std::size_t n = 48000;
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    x[i] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / 48000.0));
  }
  std::vector<float> y(n / 3 + 1);
  // Gọi theo block lẻ để kiểm tra trạng thái giữa các lần gọi.
  std::size_t produced = 0;
  for (std::size_t off = 0; off < n; off += 77) {
    const std::size_t len = std::min<std::size_t>(77, n - off);
    produced += d.process(x.data() + off, len, y.data() + produced);
  }
  ASSERT_EQ(produced, n / 3);
  y.resize(produced);
  EXPECT_NEAR(toneAmplitude(y, 1000.0, 16000.0, 100), 1.0, 0.01);

  // Độ trễ: mẫu ra j ứng với mẫu vào 3j + 2 (mẫu mới nhất) − 47,5.
  const double delayIn = 47.5;
  double maxErr = 0.0;
  for (std::size_t j = 100; j < produced; ++j) {
    const double t = static_cast<double>(3 * j + 2) - delayIn;
    const double expect = std::sin(2.0 * kPi * 1000.0 * t / 48000.0);
    maxErr = std::max(maxErr, std::fabs(expect - static_cast<double>(y[j])));
  }
  EXPECT_LT(maxErr, 0.01);
}

TEST(Decimator, AttenuatesAliases) {
  // 10 kHz vượt Nyquist mới (8 kHz) → sẽ gập về 6 kHz nếu không lọc. Phải bị chặn ≥ 55 dB.
  Decimator d(3);
  const std::size_t n = 48000;
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    x[i] = static_cast<float>(std::sin(2.0 * kPi * 10000.0 * static_cast<double>(i) / 48000.0));
  }
  std::vector<float> y(n / 3 + 1);
  y.resize(d.process(x.data(), n, y.data()));
  const double alias = toneAmplitude(y, 6000.0, 16000.0, 100);
  EXPECT_LT(20.0 * std::log10(alias), -55.0);
}
