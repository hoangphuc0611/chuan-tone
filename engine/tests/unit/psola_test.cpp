// Test bộ dịch cao độ TD-PSOLA: độ trễ cố định, độ chính xác cao độ ra, giữ formant, không click.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "dsp/fft.h"
#include "pitch/pitch_analyzer.h"
#include "pitch/psola.h"
#include "util/synth_voice.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kHop = 192;  // 4 ms

// Chạy PSOLA với cao độ "đáp án" (để kiểm riêng PSOLA, không lẫn sai số của bộ dò).
std::vector<float> runPsola(const test::SynthResult& s, double semitones, std::size_t block = 128) {
  Psola ps(PsolaConfig{});
  std::vector<float> out(s.audio.size());
  const auto ratio = static_cast<float>(std::pow(2.0, semitones / 12.0));
  std::size_t nextHop = 0;
  for (std::size_t off = 0; off < s.audio.size(); off += block) {
    const std::size_t len = std::min(block, s.audio.size() - off);
    if (off >= nextHop) {
      const float f0 = s.f0[off];
      ps.setAnalysis(f0 > 0.0f, f0 > 0.0f ? static_cast<float>(kFs / f0) : 0.0f);
      ps.setRatio(ratio);
      nextHop += kHop;
    }
    ps.process(s.audio.data() + off, out.data() + off, len);
  }
  return out;
}

// Cao độ trung vị (Hz) của đoạn [from, to) giây, đo bằng PitchAnalyzer.
double medianF0(const std::vector<float>& x, double from, double to) {
  PitchAnalyzer pa(PitchAnalyzerConfig{});
  std::vector<double> v;
  pa.process(x.data(), x.size(), [&](const PitchFrame& f) {
    if (f.timeSec >= from && f.timeSec < to && f.tracked.voiced) v.push_back(f.tracked.f0);
  });
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// Đường bao phổ (dB) bằng cepstrum: trung bình |X|² nhiều khung → log → giữ quefrency < 1,5 ms.
std::vector<double> envelopeDb(const std::vector<float>& x, std::size_t from, std::size_t to) {
  constexpr std::size_t n = 4096;
  RealFft fft(n);
  std::vector<float> frame(n);
  std::vector<std::complex<float>> spec(fft.bins());
  std::vector<double> power(fft.bins(), 0.0);
  for (std::size_t start = from; start + n <= to; start += n / 2) {
    for (std::size_t i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n);
      frame[i] = static_cast<float>(x[start + i] * w);
    }
    fft.forward(frame.data(), spec.data());
    for (std::size_t k = 0; k < spec.size(); ++k) power[k] += std::norm(spec[k]);
  }
  for (std::size_t k = 0; k < spec.size(); ++k) {
    spec[k] = {static_cast<float>(std::log(power[k] + 1e-12)), 0.0f};
  }
  std::vector<float> cep(n);
  fft.inverse(spec.data(), cep.data());
  const std::size_t lifter = static_cast<std::size_t>(0.0015 * kFs);  // 72 mẫu
  for (std::size_t i = lifter; i < n - lifter; ++i) cep[i] = 0.0f;
  fft.forward(cep.data(), spec.data());
  std::vector<double> env(spec.size());
  for (std::size_t k = 0; k < spec.size(); ++k) env[k] = 10.0 / std::log(10.0) * spec[k].real();
  return env;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b, std::size_t lo,
                   std::size_t hi) {
  double ma = 0, mb = 0;
  for (std::size_t i = lo; i < hi; ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= static_cast<double>(hi - lo);
  mb /= static_cast<double>(hi - lo);
  double sab = 0, saa = 0, sbb = 0;
  for (std::size_t i = lo; i < hi; ++i) {
    sab += (a[i] - ma) * (b[i] - mb);
    saa += (a[i] - ma) * (a[i] - ma);
    sbb += (b[i] - mb) * (b[i] - mb);
  }
  return sab / std::sqrt(saa * sbb);
}

float maxStep(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float m = 0.0f;
  for (std::size_t i = from + 1; i < to; ++i) m = std::max(m, std::fabs(x[i] - x[i - 1]));
  return m;
}

}  // namespace

TEST(Psola, UnvoicedPassesThroughWithExactLatency) {
  Psola ps(PsolaConfig{});
  EXPECT_EQ(ps.latency(), 480);
  const auto x = test::whiteNoise(0.2, kFs, 0.5);
  std::vector<float> y(x.size());
  ps.process(x.data(), y.data(), x.size());
  for (std::size_t i = 480; i < x.size(); ++i) ASSERT_EQ(y[i], x[i - 480]) << i;
  for (std::size_t i = 0; i < 480; ++i) ASSERT_EQ(y[i], 0.0f);
}

TEST(Psola, RatioOneKeepsPitchAndLevel) {
  const auto s = test::synthVoice(test::steady(220.0), 1.0);
  const auto y = runPsola(s, 0.0);
  EXPECT_LT(std::fabs(1200.0 * std::log2(medianF0(y, 0.3, 0.9) / 220.0)), 5.0);
  double ein = 0, eout = 0;
  for (std::size_t i = 14400; i < 43200; ++i) {
    ein += s.audio[i] * s.audio[i];
    eout += y[i] * y[i];
  }
  EXPECT_NEAR(10.0 * std::log10(eout / ein), 0.0, 1.0);
}

TEST(Psola, ShiftAccuracyAcrossVoices) {
  // Giọng nam trầm (110 Hz, grain bị cắt ngắn), giọng nam (165), nữ (330).
  for (double f0 : {110.0, 165.0, 330.0}) {
    const auto s = test::synthVoice(test::steady(f0), 1.2);
    for (double st : {-6.0, -3.0, -1.0, 1.0, 3.0, 6.0}) {
      const auto y = runPsola(s, st);
      const double want = f0 * std::pow(2.0, st / 12.0);
      const double got = medianF0(y, 0.3, 1.1);
      EXPECT_LT(std::fabs(1200.0 * std::log2(got / want)), 5.0) << "f0=" << f0 << " st=" << st;
    }
  }
}

TEST(Psola, PreservesFormants) {
  test::VoiceSpec spec;
  spec.vowel = test::Vowel::A;
  const auto s = test::synthVoice(test::steady(200.0), 1.5, spec);
  const auto envIn = envelopeDb(s.audio, 9600, s.audio.size());
  const std::size_t lo = static_cast<std::size_t>(200.0 / kFs * 4096);
  const std::size_t hi = static_cast<std::size_t>(4000.0 / kFs * 4096);
  for (double st : {-6.0, -3.0, 3.0, 6.0}) {
    const auto y = runPsola(s, st);
    const auto envOut = envelopeDb(y, 9600, y.size());
    // Đo được ≥ 0,99 (2026-10-21).
    EXPECT_GT(correlation(envIn, envOut, lo, hi), 0.95) << st;
  }
  // Đối chứng: chỉ đổi tốc độ phát (formant dịch theo) phải cho tương quan thấp hơn rõ.
  std::vector<double> scaled(envIn.size());
  const double r = std::pow(2.0, 6.0 / 12.0);
  for (std::size_t k = 0; k < scaled.size(); ++k) {
    const double src = static_cast<double>(k) / r;
    const auto i0 = static_cast<std::size_t>(src);
    const double fr = src - static_cast<double>(i0);
    scaled[k] = envIn[i0] + fr * (envIn[std::min(i0 + 1, envIn.size() - 1)] - envIn[i0]);
  }
  // Đo được ≈ 0,80 → phép đo phân biệt rõ có/không giữ formant.
  EXPECT_LT(correlation(envIn, scaled, lo, hi), 0.85);
}

TEST(Psola, NoClicks) {
  const auto s = test::synthVoice(test::steady(196.0), 1.0);
  const float inStep = maxStep(s.audio, 9600, s.audio.size());
  for (double st : {-3.0, 2.0, 5.0}) {
    const auto y = runPsola(s, st);
    const float outStep = maxStep(y, 9600, y.size());
    // Cao độ cao hơn → độ dốc lớn hơn theo tỉ lệ; cho thêm 50% biên.
    const double allowed = inStep * std::max(1.0, std::pow(2.0, st / 12.0)) * 1.5;
    EXPECT_LT(outStep, allowed) << st;
  }
}

TEST(Psola, VoicedUnvoicedTransitionsStayBounded) {
  // Thanh ngã có đoạn ngắt thanh hầu, thanh nặng có tắc cuối → chuyển hữu/vô thanh liên tục.
  for (auto tone : {test::VietTone::Nga, test::VietTone::Nang}) {
    const auto s = test::synthVoice(test::vietTone(tone, 180.0), 2.0);
    const auto y = runPsola(s, 1.0, 96);
    float peakIn = 0.0f, peakOut = 0.0f;
    for (float v : s.audio) peakIn = std::max(peakIn, std::fabs(v));
    for (float v : y) {
      ASSERT_TRUE(std::isfinite(v));
      peakOut = std::max(peakOut, std::fabs(v));
    }
    EXPECT_LT(peakOut, peakIn * 1.5f);
  }
}

TEST(Psola, FollowsGlide) {
  // Trượt 196 → 262 Hz, dịch +2 nửa cung: đầu ra phải bám theo (sai số trung vị ≤ 10 cent).
  const auto s = test::synthVoice(test::glide(196.0, 262.0, 0.2, 1.0), 1.4);
  const auto y = runPsola(s, 2.0);
  PitchAnalyzer pa(PitchAnalyzerConfig{});
  std::vector<double> errs;
  const double r = std::pow(2.0, 2.0 / 12.0);
  pa.process(y.data(), y.size(), [&](const PitchFrame& f) {
    if (f.timeSec < 0.15 || f.timeSec > 1.3 || !f.tracked.voiced) return;
    // Đầu ra trễ 10 ms so với đầu vào.
    const auto idx = static_cast<std::size_t>((f.timeSec - 0.01) * kFs);
    errs.push_back(std::fabs(1200.0 * std::log2(f.tracked.f0 / (s.f0[idx] * r))));
  });
  std::sort(errs.begin(), errs.end());
  ASSERT_FALSE(errs.empty());
  EXPECT_LT(errs[errs.size() / 2], 10.0);
}
