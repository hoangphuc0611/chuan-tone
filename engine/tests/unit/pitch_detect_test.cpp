// Test dò cao độ (MPM / YIN / F0Tracker / PitchAnalyzer) trên tín hiệu có đáp án biết trước.
// Chỉ tiêu (CLAUDE.md 3.10): sai số ≤ 5 cent trên sóng sin và giọng mẫu.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "pitch/f0_tracker.h"
#include "pitch/mpm.h"
#include "pitch/pitch_analyzer.h"
#include "pitch/yin.h"
#include "util/synth_voice.h"

using namespace tonecore;
using tonecore::test::SynthResult;
using tonecore::test::VoiceSpec;
using tonecore::test::Vowel;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;

struct Stats {
  double medianCents = 0.0;
  double p95Cents = 0.0;
  double grossRate = 0.0;   // tỉ lệ khung lệch > 50 cent
  int octaveErrors = 0;     // khung lệch ~1200 cent (±100)
  double recall = 0.0;      // khung hữu thanh (đáp án) được nhận là hữu thanh
  double falseAlarm = 0.0;  // khung vô thanh (đáp án) bị nhận là hữu thanh
  int frames = 0;
};

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
  return v[idx];
}

// Chạy PitchAnalyzer trên `audio` và so với đáp án f0 từng mẫu. Một khung được tính là "hữu
// thanh" theo đáp án khi toàn bộ cửa sổ quanh tâm đều hữu thanh (bỏ khung giáp ranh).
Stats evaluate(const std::vector<float>& audio, const std::vector<float>& truth,
               PitchAlgorithm algo = PitchAlgorithm::Mpm, double skipSec = 0.06) {
  PitchAnalyzerConfig cfg;
  cfg.algorithm = algo;
  PitchAnalyzer pa(cfg);
  const auto half = static_cast<long>(cfg.windowMs * 1e-3 * kFs / 2);
  std::vector<double> errs;
  int voicedTruth = 0, hit = 0, unvoicedTruth = 0, fa = 0, gross = 0, octave = 0;
  pa.process(audio.data(), audio.size(), [&](const PitchFrame& f) {
    if (f.timeSec < skipSec) return;
    const auto c = static_cast<long>(std::lround(f.timeSec * kFs));
    if (c - half < 0 || c + half >= static_cast<long>(truth.size())) return;
    bool allVoiced = true, allUnvoiced = true;
    for (long i = c - half; i <= c + half; i += 16) {
      if (truth[static_cast<std::size_t>(i)] > 0.0f) {
        allUnvoiced = false;
      } else {
        allVoiced = false;
      }
    }
    if (allVoiced) {
      ++voicedTruth;
      if (!f.tracked.voiced) return;
      ++hit;
      const double cents = 1200.0 * std::log2(f.tracked.f0 / truth[static_cast<std::size_t>(c)]);
      errs.push_back(std::fabs(cents));
      if (std::fabs(cents) > 50.0) ++gross;
      if (std::fabs(std::fabs(cents) - 1200.0) < 100.0) ++octave;
    } else if (allUnvoiced) {
      ++unvoicedTruth;
      if (f.tracked.voiced) ++fa;
    }
  });
  Stats s;
  s.frames = static_cast<int>(errs.size());
  s.medianCents = percentile(errs, 0.5);
  s.p95Cents = percentile(errs, 0.95);
  s.grossRate = errs.empty() ? 0.0 : static_cast<double>(gross) / static_cast<double>(errs.size());
  s.octaveErrors = octave;
  s.recall = voicedTruth > 0 ? static_cast<double>(hit) / voicedTruth : 1.0;
  s.falseAlarm = unvoicedTruth > 0 ? static_cast<double>(fa) / unvoicedTruth : 0.0;
  return s;
}

std::vector<float> sine16k(double f, std::size_t n) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * f * static_cast<double>(i) / 16000.0));
  }
  return v;
}

double centsErr(double est, double truth) {
  return 1200.0 * std::log2(est / truth);
}

}  // namespace

// ---------------- MPM / YIN trên một khung ----------------

TEST(Mpm, SineAccuracyAcrossRange) {
  PitchDetectorConfig cfg;
  Mpm mpm(cfg);
  for (double f : {75.0, 98.0, 147.0, 220.0, 330.0, 440.0, 660.0, 880.0, 1046.0}) {
    const auto x = sine16k(f, cfg.window);
    const auto e = mpm.analyze(x.data());
    ASSERT_GT(e.f0, 0.0f) << f;
    EXPECT_LT(std::fabs(centsErr(e.f0, f)), 5.0) << f;
    EXPECT_GT(e.clarity, 0.95f) << f;
  }
}

TEST(Mpm, SawtoothNoOctaveError) {
  // Răng cưa có hoà âm mạnh — kiểu tín hiệu dễ gây lỗi quãng 8.
  PitchDetectorConfig cfg;
  Mpm mpm(cfg);
  for (double f : {90.0, 180.0, 400.0, 800.0}) {
    std::vector<float> x(cfg.window);
    for (std::size_t i = 0; i < x.size(); ++i) {
      const double ph = std::fmod(f * static_cast<double>(i) / 16000.0, 1.0);
      x[i] = static_cast<float>(0.5 * (2.0 * ph - 1.0));
    }
    const auto e = mpm.analyze(x.data());
    EXPECT_LT(std::fabs(centsErr(e.f0, f)), 5.0) << f;
  }
}

TEST(Mpm, NoiseHasLowClarity) {
  PitchDetectorConfig cfg;
  Mpm mpm(cfg);
  auto noise = tonecore::test::whiteNoise(0.04, 16000.0, 0.5);
  noise.resize(cfg.window);
  EXPECT_LT(mpm.analyze(noise.data()).clarity, 0.6f);
  std::vector<float> silence(cfg.window, 0.0f);
  EXPECT_EQ(mpm.analyze(silence.data()).f0, 0.0f);
}

TEST(Yin, SineAccuracy) {
  PitchDetectorConfig cfg;
  Yin yin(cfg);
  for (double f : {80.0, 220.0, 440.0, 1000.0}) {
    const auto x = sine16k(f, cfg.window);
    const auto e = yin.analyze(x.data());
    EXPECT_LT(std::fabs(centsErr(e.f0, f)), 5.0) << f;
    EXPECT_GT(e.clarity, 0.9f) << f;
  }
}

// ---------------- F0Tracker ----------------

TEST(F0Tracker, RejectsSingleOctaveGlitch) {
  F0Tracker tr;
  PitchEstimate e{220.0f, 0.95f};
  TrackedPitch out;
  for (int i = 0; i < 10; ++i) out = tr.update(e, -20.0f, 0.05f);
  EXPECT_NEAR(out.f0, 220.0f, 0.5f);
  // Một (hai) khung nhảy lên quãng 8 rồi trở lại → đầu ra không đổi.
  for (int i = 0; i < 2; ++i) {
    out = tr.update({440.0f, 0.95f}, -20.0f, 0.05f);
    EXPECT_NEAR(out.f0, 220.0f, 0.5f);
  }
  out = tr.update(e, -20.0f, 0.05f);
  EXPECT_NEAR(out.f0, 220.0f, 0.5f);
}

TEST(F0Tracker, AcceptsSustainedLeap) {
  F0Tracker tr;
  for (int i = 0; i < 10; ++i) tr.update({220.0f, 0.95f}, -20.0f, 0.05f);
  TrackedPitch out;
  int framesToAccept = -1;
  for (int i = 0; i < 10; ++i) {
    out = tr.update({440.0f, 0.95f}, -20.0f, 0.05f);
    if (framesToAccept < 0 && std::fabs(out.f0 - 440.0f) < 1.0f) framesToAccept = i + 1;
  }
  EXPECT_NEAR(out.f0, 440.0f, 0.5f);
  EXPECT_LE(framesToAccept, 3);
}

TEST(F0Tracker, VoicingHysteresisAndGates) {
  F0Tracker tr;
  // Clarity 0,7: chưa đủ để bắt đầu (0,8).
  EXPECT_FALSE(tr.update({200.0f, 0.7f}, -20.0f, 0.05f).voiced);
  EXPECT_TRUE(tr.update({200.0f, 0.9f}, -20.0f, 0.05f).voiced);
  // Đang hữu thanh: 0,78 vẫn giữ được (ngưỡng giữ 0,75) dù dưới ngưỡng bắt đầu.
  EXPECT_TRUE(tr.update({200.0f, 0.78f}, -20.0f, 0.05f).voiced);
  // Quá nhỏ → vô thanh.
  EXPECT_FALSE(tr.update({200.0f, 0.95f}, -70.0f, 0.05f).voiced);
  // Tỉ lệ qua 0 cao + clarity vừa phải → tiếng xát.
  tr.update({200.0f, 0.95f}, -20.0f, 0.05f);
  EXPECT_FALSE(tr.update({200.0f, 0.8f}, -20.0f, 0.45f).voiced);
}

// ---------------- PitchAnalyzer trên giọng tổng hợp ----------------

TEST(PitchAnalyzer, SyntheticVowelsAcrossRange) {
  // Từ Mi2 (82 Hz, giọng nam trầm) tới Đô6 (1047 Hz, giọng nữ cao).
  for (Vowel v : {Vowel::A, Vowel::I, Vowel::U}) {
    for (int midi = 40; midi <= 84; midi += 4) {
      VoiceSpec spec;
      spec.vowel = v;
      spec.seed = static_cast<uint32_t>(midi);
      const auto s = tonecore::test::synthVoice(
          tonecore::test::steady(tonecore::test::midiToHz(midi)), 0.6, spec);
      const Stats st = evaluate(s.audio, s.f0);
      SCOPED_TRACE(testing::Message() << "vowel=" << static_cast<int>(v) << " midi=" << midi);
      EXPECT_GT(st.frames, 100);
      EXPECT_LE(st.medianCents, 5.0);
      EXPECT_LT(st.grossRate, 0.01);
      EXPECT_EQ(st.octaveErrors, 0);
      EXPECT_GT(st.recall, 0.95);
    }
  }
}

TEST(PitchAnalyzer, TracksVibratoAndGlide) {
  const auto vib = tonecore::test::synthVoice(tonecore::test::vibrato(262.0, 5.5, 50.0), 2.0);
  const Stats sv = evaluate(vib.audio, vib.f0);
  EXPECT_LE(sv.medianCents, 8.0);
  EXPECT_LE(sv.p95Cents, 25.0);
  EXPECT_EQ(sv.octaveErrors, 0);

  // Trượt một quãng 8 trong 1 giây (luyến rộng).
  const auto gl = tonecore::test::synthVoice(tonecore::test::glide(196.0, 392.0, 0.3, 1.0), 1.6);
  const Stats sg = evaluate(gl.audio, gl.f0);
  EXPECT_LE(sg.medianCents, 10.0);
  EXPECT_EQ(sg.octaveErrors, 0);
  EXPECT_GT(sg.recall, 0.95);
}

TEST(PitchAnalyzer, VietnameseTones) {
  using tonecore::test::VietTone;
  for (VietTone t : {VietTone::Ngang, VietTone::Huyen, VietTone::Sac, VietTone::Hoi, VietTone::Nga,
                     VietTone::Nang}) {
    const auto s = tonecore::test::synthVoice(tonecore::test::vietTone(t, 196.0), 2.0);
    const Stats st = evaluate(s.audio, s.f0);
    SCOPED_TRACE(testing::Message() << "tone=" << static_cast<int>(t));
    EXPECT_LE(st.medianCents, 12.0);
    EXPECT_EQ(st.octaveErrors, 0);
    EXPECT_GT(st.recall, 0.9);
  }
}

TEST(PitchAnalyzer, UnvoicedSoundsRejected) {
  const std::vector<float> silenceTruth(static_cast<std::size_t>(kFs), 0.0f);
  const std::vector<float> silence(static_cast<std::size_t>(kFs), 0.0f);
  EXPECT_LE(evaluate(silence, silenceTruth).falseAlarm, 0.0);
  EXPECT_LE(evaluate(tonecore::test::whiteNoise(1.0, kFs, 0.3), silenceTruth).falseAlarm, 0.05);
  EXPECT_LE(evaluate(tonecore::test::fricativeS(1.0, kFs, 0.3), silenceTruth).falseAlarm, 0.05);
}

TEST(PitchAnalyzer, YinComparable) {
  const auto s = tonecore::test::synthVoice(tonecore::test::vibrato(220.0), 1.0);
  const Stats st = evaluate(s.audio, s.f0, PitchAlgorithm::Yin);
  EXPECT_LE(st.medianCents, 10.0);
  EXPECT_EQ(st.octaveErrors, 0);
}

TEST(PitchAnalyzer, FrameTimingMatchesInput) {
  // Nốt đổi đột ngột tại 0,5 s: khung đầu tiên báo nốt mới phải nằm gần 0,5 s (± nửa cửa sổ).
  auto contour = [](double t) { return t < 0.5 ? 220.0 : 330.0; };
  const auto s = tonecore::test::synthVoice(contour, 1.0);
  PitchAnalyzer pa(PitchAnalyzerConfig{});
  double firstNew = -1.0;
  pa.process(s.audio.data(), s.audio.size(), [&](const PitchFrame& f) {
    if (firstNew < 0.0 && f.tracked.voiced && f.tracked.f0 > 300.0f) firstNew = f.timeSec;
  });
  EXPECT_GT(firstNew, 0.5 - 0.005);
  EXPECT_LT(firstNew, 0.5 + 0.03);
}

TEST(F0Tracker, OnsetNeedsConfirmationUnlessVeryClear) {
  F0Tracker tr;
  // Khung đầu clarity 0,85 (< 0,9): chưa báo hữu thanh; khung sau cùng nốt → xác nhận.
  EXPECT_FALSE(tr.update({200.0f, 0.85f}, -20.0f, 0.05f).voiced);
  EXPECT_TRUE(tr.update({201.0f, 0.85f}, -20.0f, 0.05f).voiced);
  // Khung đầu sai quãng 8 rồi khung sau đúng → không bao giờ báo giá trị sai.
  F0Tracker tr2;
  EXPECT_FALSE(tr2.update({400.0f, 0.82f}, -20.0f, 0.05f).voiced);
  EXPECT_FALSE(tr2.update({200.0f, 0.85f}, -20.0f, 0.05f).voiced);
  const auto out = tr2.update({200.0f, 0.85f}, -20.0f, 0.05f);
  EXPECT_TRUE(out.voiced);
  EXPECT_NEAR(out.f0, 200.0f, 0.5f);
  // Khung rất rõ: báo ngay.
  F0Tracker tr3;
  EXPECT_TRUE(tr3.update({300.0f, 0.95f}, -20.0f, 0.05f).voiced);
}
