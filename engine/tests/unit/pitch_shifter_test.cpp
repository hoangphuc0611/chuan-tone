// Đổi tone beat (M4): PitchShifter + chỉ số của bộ tín hiệu tổng hợp (tests/util/stretch_eval.h).
// Chỉ tiêu: docs/stretch-results.md. B1: cao độ, chia khối, mono. B2 (khoá pha): căn thời gian,
// âm lượng phần có cao độ, tạp phổ, ảnh stereo, mức nhiễu. B3 (giữ transient): trống không nhòe.

#include "stretch/pitch_shifter.h"

#include <gtest/gtest.h>

#include <cmath>

#include "util/stretch_eval.h"
#include "util/synth_music.h"

using namespace tonecore;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

const test::StretchSignal& signal(const char* name) {
  static const auto suite = test::stretchSuite(kRate);
  for (const auto& s : suite) {
    if (s.name == name) return s;
  }
  throw std::runtime_error(name);
}

}  // namespace

TEST(PitchShifter, ZeroSemitonesIsBitExact) {
  for (const auto& sig : test::stretchSuite(kRate)) {
    const auto y = pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, 0.0);
    EXPECT_EQ(y, sig.stereo) << sig.name;
  }
}

// Cao độ đúng tới 1 cent ở các mức −6…+6 (sin và nốt có hoạ âm).
TEST(PitchShifter, PitchIsExact) {
  for (const char* name : {"sine", "harmonic"}) {
    const auto& sig = signal(name);
    for (const int n : {-6, -1, 1, 6}) {
      const auto y = pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, n);
      ASSERT_EQ(y.size(), sig.stereo.size());
      EXPECT_LE(test::measureStretch(sig, y, n).pitchErrorCents, 1.0) << name << " " << n;
    }
  }
}

// Cách chia khối khi pull() không đổi kết quả.
TEST(PitchShifter, ChunkingDoesNotChangeOutput) {
  const auto& sig = signal("beat");
  const uint64_t frames = static_cast<uint64_t>(2.0 * kRate);
  const auto whole = pitchShiftBuffer(sig.stereo.data(), frames, 2, 3.0);
  for (const uint32_t chunk : {1u, 37u, 1000u}) {
    PitchShifter ps(2, 3.0);
    ps.start(sig.stereo.data(), frames, 0);
    std::vector<float> y(whole.size());
    for (uint64_t pos = 0; pos < frames; pos += chunk) {
      const auto n = static_cast<uint32_t>(std::min<uint64_t>(chunk, frames - pos));
      ps.pull(y.data() + 2 * pos, n);
    }
    EXPECT_EQ(y, whole) << "chunk " << chunk;
  }
}

// Bản mới khớp thời gian với bản gốc: tâm năng lượng của một "cục" sin (bao Gauss σ 50 ms) giữ
// nguyên vị trí ±0,5 ms ở mọi mức. Phase vocoder cơ bản (B1) lệch (1 − β)·~35 ms vì độ nghiêng pha
// giữa các bin bị nhân β; khoá pha identity lấy lại quan hệ đó từ phổ phân tích mỗi khung.
TEST(PitchShifter, TimeAlignedWithOriginal) {
  const std::size_t frames = static_cast<std::size_t>(2.0 * kRate);
  std::vector<float> x(2 * frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / kRate;
    const double env = std::exp(-0.5 * std::pow((t - 1.0) / 0.05, 2));
    const auto v = static_cast<float>(0.5 * env * std::sin(2 * kPi * 330.0 * t));
    x[2 * i] = v;
    x[2 * i + 1] = v;
  }
  const auto centroid = [&](const std::vector<float>& y) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
      const double e = static_cast<double>(y[2 * i]) * y[2 * i];
      num += e * static_cast<double>(i);
      den += e;
    }
    return 1000.0 * num / den / kRate;
  };
  const double want = centroid(x);
  for (const int n : {-6, -3, -1, 1, 3, 6}) {
    const auto y = pitchShiftBuffer(x.data(), frames, 2, n);
    EXPECT_NEAR(centroid(y), want, 0.5) << n;
  }
}

// Beat mono (L = R) ra vẫn đúng L = R.
TEST(PitchShifter, MonoStaysMono) {
  const auto& sig = signal("drums");
  const auto y = pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, -4.0);
  for (std::size_t i = 0; i < sig.frames(); ++i) ASSERT_EQ(y[2 * i], y[2 * i + 1]) << i;
}

// Phần có cao độ (sin ổn định, hợp âm tắt dần) giữ đúng âm lượng ±0,1 dB.
TEST(PitchShifter, TonalLevelPreserved) {
  const std::size_t frames = static_cast<std::size_t>(3.0 * kRate);
  std::vector<float> x(2 * frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const auto v =
        static_cast<float>(0.4 * std::sin(2 * kPi * 523.25 * static_cast<double>(i) / kRate));
    x[2 * i] = v;
    x[2 * i + 1] = v;
  }
  const auto power = [&](const std::vector<float>& y) {
    double e = 0.0;
    for (std::size_t i = frames / 4; i < 3 * frames / 4; ++i)
      e += static_cast<double>(y[2 * i]) * y[2 * i];
    return e;
  };
  for (const int n : {-6, -2, 1, 5}) {
    const auto y = pitchShiftBuffer(x.data(), frames, 2, n);
    EXPECT_NEAR(10.0 * std::log10(power(y) / power(x)), 0.0, 0.1) << n;
  }
}

// Tạp phổ: năng lượng ngoài các hoạ âm đã dịch ≤ −40 dB (một nốt) / −35 dB (hợp âm).
TEST(PitchShifter, LowSpectralClutter) {
  for (const int n : {-6, 2, 6}) {
    const auto& note = signal("harmonic");
    const auto& chord = signal("chord");
    EXPECT_LE(
        test::measureStretch(note, pitchShiftBuffer(note.stereo.data(), note.frames(), 2, n), n)
            .offPartialDb,
        -40.0)
        << n;
    EXPECT_LE(
        test::measureStretch(chord, pitchShiftBuffer(chord.stereo.data(), chord.frames(), 2, n), n)
            .offPartialDb,
        -35.0)
        << n;
  }
}

// Ảnh stereo: tương quan L–R của beat có pan lệch ≤ 0,02 so với bản gốc (phase vocoder cơ bản xử
// lý từng kênh riêng làm lệch 0,6–0,9).
TEST(PitchShifter, StereoImagePreserved) {
  const auto& sig = signal("beat");
  for (const int n : {-6, 4}) {
    const auto y = pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, n);
    EXPECT_LE(test::measureStretch(sig, y, n).correlationDelta, 0.02) << n;
  }
}

// Nhiễu trắng không hụt quá 1,5 dB so với lý tưởng (các khung nhiễu chồng nhau không đồng pha nên
// luôn hụt một chút; phase vocoder cơ bản hụt ~3 dB).
TEST(PitchShifter, NoiseLevelNearIdeal) {
  const auto& sig = signal("noise");
  for (const int n : {-6, -3, 3, 6}) {
    const auto y = pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, n);
    EXPECT_GE(test::measureStretch(sig, y, n).noiseLevelDb, -1.5) << n;
  }
}

// Trống không nhòe (B3): năng lượng 5–50 ms trước tiếng đánh ≤ −30 dB (trung vị) / −25 dB (tệ
// nhất), tiếng đánh không chậm thêm quá 2 ms (3 ms ở ±5…6), không "đánh đôi" (dội cao tần ≤ −12
// dB), bắt đầu đúng chỗ ±1 ms. Không giữ transient (B2) các số này là −21…−29 dB, +7…17 ms, lệch
// 10–17 ms.
TEST(PitchShifter, DrumsStaySharp) {
  const auto& sig = signal("drums");
  for (const int n : {-6, -2, 3, 6}) {
    const auto m =
        test::measureStretch(sig, pitchShiftBuffer(sig.stereo.data(), sig.frames(), 2, n), n);
    EXPECT_LE(m.preEchoDb, -30.0) << n;
    EXPECT_LE(m.preEchoWorstDb, -25.0) << n;
    EXPECT_LE(m.riseDeltaMs, std::abs(n) >= 5 ? 3.0 : 2.0) << n;
    EXPECT_LE(m.reboundDb, -12.0) << n;
    EXPECT_LE(m.lagMaxMs, 1.0) << n;
  }
}

// Một tiếng kick ở 12 vị trí lệch nhau 2,6 ms (bước phân tích 15–30 ms): 0,5–8 ms trước tiếng
// đánh không có gì to hơn −30 dB so với đỉnh. (Lỗi đã gặp: cửa sổ Hann bắt đầu đúng tại onset xoá
// mất tiếng click < 1 ms khi phân loại bin → các bin cao tần không được căn, rò −7…−26 dB.)
TEST(PitchShifter, KickHasNoPreEchoAtAnyGridOffset) {
  const double rate = kRate;
  for (const int n : {-6, 6}) {
    for (int k = 0; k < 12; ++k) {
      const double t = 0.5 + 0.0026 * k;
      std::vector<float> mono(static_cast<std::size_t>(0.8 * rate));
      test::addDrum(mono, rate, t, test::Drum::Kick, 0.5f, 7);
      std::vector<float> x(2 * mono.size());
      for (std::size_t i = 0; i < mono.size(); ++i) x[2 * i] = x[2 * i + 1] = mono[i];
      const auto y = pitchShiftBuffer(x.data(), mono.size(), 2, n);
      const auto t0 = static_cast<std::size_t>(std::llround(t * rate));
      float peak = 0.0f;
      float pre = 0.0f;
      for (std::size_t i = t0; i < t0 + 1440; ++i) peak = std::max(peak, std::abs(y[2 * i]));
      for (std::size_t i = t0 - 384; i < t0 - 24; ++i) pre = std::max(pre, std::abs(y[2 * i]));
      EXPECT_LE(20.0 * std::log10(pre / peak), -30.0) << n << " offset " << k;
    }
  }
}

// Chỉ số trên tín hiệu chưa đổi: không có tiếng trước onset, tiếng đánh lên trong ~1 ms (bằng
// cửa sổ bao biên), cao độ sin đo đúng.
TEST(StretchMetrics, ReferenceValuesAreSane) {
  const auto drums = test::measureReference(signal("drums"));
  EXPECT_LT(drums.preEchoDb, -100.0);
  EXPECT_LT(drums.preEchoWorstDb, -40.0);
  EXPECT_LT(drums.riseDeltaMs, 1.5);
  EXPECT_LT(drums.reboundDb, -15.0);
  EXPECT_EQ(drums.lagMaxMs, 0.0);
  EXPECT_LT(test::measureReference(signal("sine")).pitchErrorCents, 0.01);
  EXPECT_LT(test::measureReference(signal("harmonic")).offPartialDb, -55.0);
}
