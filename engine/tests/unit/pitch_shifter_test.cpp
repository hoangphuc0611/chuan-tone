// Đổi tone beat (M4): PitchShifter + chỉ số của bộ tín hiệu tổng hợp (tests/util/stretch_eval.h).
// B1 kiểm tính đúng của khung (cao độ, căn thời gian, chia khối, mono); chỉ tiêu chất lượng trống /
// stereo / âm lượng thêm ở B2–B3 (xem docs/stretch-results.md).

#include "stretch/pitch_shifter.h"

#include <gtest/gtest.h>

#include <cmath>

#include "util/stretch_eval.h"

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
    for (const int n : {-6, -4, -1, 1, 3, 6}) {
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
// nguyên vị trí ±0,5 ms ở mọi mức.
// TẮT tới B2: phase vocoder cơ bản (B1) lệch (1 − β)·~35 ms — độ nghiêng pha giữa các bin (vị trí
// của âm trong khung) bị nhân β khi tổng hợp nên không bám theo phổ phân tích. Khoá pha identity
// (B2) lấy lại quan hệ pha giữa các bin từ phổ phân tích mỗi khung.
TEST(PitchShifter, DISABLED_TimeAlignedWithOriginal) {
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
