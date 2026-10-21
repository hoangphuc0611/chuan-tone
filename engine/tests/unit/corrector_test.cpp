// Test thang âm, lượng tử nốt và đường cong chỉnh giọng (không cần audio: đưa thẳng cao độ).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "pitch/corrector.h"
#include "pitch/presets.h"
#include "pitch/scales.h"
#include "util/synth_voice.h"

using namespace tonecore;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrameRate = 250.0;  // hop 4 ms

struct Run {
  std::vector<float> in;   // cao độ vào (MIDI), NaN = vô thanh
  std::vector<float> out;  // cao độ ra = vào + lượng chỉnh
  std::vector<int> target;
};

// Chạy corrector trên đường cao độ (MIDI theo thời gian, NaN = vô thanh).
Run run(const CorrectorParams& p, const std::function<double(double)>& midiAt, double seconds,
        uint16_t mask = keyMask(0, Scale::Major)) {
  PitchCorrector c(kFrameRate);
  c.setParams(p);
  c.setKeyMask(mask);
  Run r;
  const int n = static_cast<int>(seconds * kFrameRate);
  for (int i = 0; i < n; ++i) {
    const double m = midiAt(i / kFrameRate);
    TrackedPitch tp;
    if (!std::isnan(m)) {
      tp.voiced = true;
      tp.midi = static_cast<float>(m);
      tp.f0 = static_cast<float>(test::midiToHz(m));
      tp.confidence = 0.95f;
    }
    const auto f = c.update(tp);
    r.in.push_back(static_cast<float>(m));
    r.out.push_back(static_cast<float>(m) + f.correction);
    r.target.push_back(f.targetNote);
  }
  return r;
}

double mean(const std::vector<float>& v, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i) s += v[i];
  return s / static_cast<double>(to - from);
}

double amplitude(const std::vector<float>& v, std::size_t from, std::size_t to) {
  const double mu = mean(v, from, to);
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i) s += (v[i] - mu) * (v[i] - mu);
  return std::sqrt(2.0 * s / static_cast<double>(to - from));  // biên độ sin tương đương
}

}  // namespace

// ---------------- Thang âm ----------------

TEST(Scales, KeyMasks) {
  // La thứ tự nhiên = các nốt trắng = Đô trưởng.
  EXPECT_EQ(keyMask(9, Scale::NaturalMinor), keyMask(0, Scale::Major));
  EXPECT_EQ(keyMask(0, Scale::Major), 0b101010110101);
  EXPECT_EQ(keyMask(0, Scale::Chromatic), 0x0FFF);
  // Ngũ cung Sol trưởng: Sol La Si Rê Mi = 7 9 11 2 4.
  const uint16_t g = keyMask(7, Scale::PentatonicMajor);
  for (int pc : {7, 9, 11, 2, 4}) EXPECT_TRUE((g >> pc) & 1) << pc;
  for (int pc : {0, 5, 6}) EXPECT_FALSE((g >> pc) & 1) << pc;
  // Thứ hoà âm La: có Sol# (8), không có Sol (7).
  const uint16_t hm = keyMask(9, Scale::HarmonicMinor);
  EXPECT_TRUE((hm >> 8) & 1);
  EXPECT_FALSE((hm >> 7) & 1);
}

TEST(Scales, NearestNote) {
  const uint16_t c = keyMask(0, Scale::Major);
  EXPECT_EQ(nearestNoteInMask(60.2f, c), 60);
  EXPECT_EQ(nearestNoteInMask(61.4f, c), 62);  // Đô# không thuộc Đô trưởng
  EXPECT_EQ(nearestNoteInMask(61.0f, c), 60);  // cách đều → chọn nốt thấp
  EXPECT_EQ(nearestNoteInMask(64.6f, c), 65);  // Mi–Fa cách nửa cung
  // Ngũ cung Đô: 60 62 64 67 69 → 65,6 gần 64 (1,6) hơn 67 (1,4)? Không: gần 67.
  EXPECT_EQ(nearestNoteInMask(65.6f, keyMask(0, Scale::PentatonicMajor)), 67);
  EXPECT_EQ(nearestNoteInMask(65.4f, keyMask(0, Scale::PentatonicMajor)), 64);
}

TEST(Scales, QuantizerHysteresis) {
  NoteQuantizer q;
  q.setMask(keyMask(0, Scale::Chromatic));
  EXPECT_EQ(q.quantize(60.0f, 20.0f), 60);
  // Vượt ranh giới 60,5 nhưng chưa quá 20 cent → giữ 60.
  EXPECT_EQ(q.quantize(60.65f, 20.0f), 60);
  // Quá 20 cent → chuyển.
  EXPECT_EQ(q.quantize(60.75f, 20.0f), 61);
  // Quay lại 60,4 (chưa quá ranh giới về phía 60 thêm 20 cent) → vẫn 61.
  EXPECT_EQ(q.quantize(60.4f, 20.0f), 61);
  EXPECT_EQ(q.quantize(60.25f, 20.0f), 60);
}

// ---------------- Corrector ----------------

TEST(Corrector, RobotSnapsImmediately) {
  const auto p = correctionPreset(CorrectionPreset::Robot);
  const auto r = run(p, [](double) { return 69.3; }, 0.5);
  for (std::size_t i = 0; i < r.out.size(); ++i) {
    EXPECT_NEAR(r.out[i], 69.0f, 0.03f) << i;  // ±3 cent từ khung đầu
  }
}

TEST(Corrector, SingerReachesTargetWithin50ms) {
  const auto p = correctionPreset(CorrectionPreset::Singer);
  const auto r = run(p, [](double) { return 69.3; }, 0.5);
  const auto i50 = static_cast<std::size_t>(0.05 * kFrameRate);
  for (std::size_t i = i50; i < r.out.size(); ++i) {
    EXPECT_NEAR(r.out[i], 69.0f, 0.05f) << i;
  }
  // Không giật tức thì: khung đầu chỉ được kéo một phần.
  EXPECT_GT(r.out[0] - 69.0f, 0.1f);
}

TEST(Corrector, NaturalKeepsVibratoAndCentersNote) {
  const auto p = correctionPreset(CorrectionPreset::Natural);
  // Rung giọng 5,5 Hz ±50 cent quanh La4 + 30 cent (hát hơi cao).
  const auto r = run(p, [](double t) { return 69.3 + 0.5 * std::sin(2 * kPi * 5.5 * t); }, 3.0);
  const std::size_t from = static_cast<std::size_t>(1.0 * kFrameRate);
  const double ratio = amplitude(r.out, from, r.out.size()) / amplitude(r.in, from, r.in.size());
  EXPECT_GT(ratio, 0.8);
  EXPECT_LT(ratio, 1.25);
  EXPECT_NEAR(mean(r.out, from, r.out.size()), 69.0, 0.05);  // tâm kéo về nốt (±5 cent)
}

TEST(Corrector, RobotFlattensVibrato) {
  const auto p = correctionPreset(CorrectionPreset::Robot);
  const auto r = run(p, [](double t) { return 69.0 + 0.4 * std::sin(2 * kPi * 5.5 * t); }, 2.0);
  EXPECT_LT(amplitude(r.out, 50, r.out.size()), 0.01);
}

TEST(Corrector, HysteresisPreventsFlipping) {
  CorrectorParams p = correctionPreset(CorrectionPreset::Robot);
  p.hysteresisCents = 20.0f;
  // Lơ lửng quanh ranh giới Đô–Rê (61,0 trong Đô trưởng) ±15 cent.
  const auto r = run(p, [](double t) { return 61.0 + 0.15 * std::sin(2 * kPi * 1.5 * t); }, 2.0);
  int changes = 0;
  for (std::size_t i = 1; i < r.target.size(); ++i) changes += r.target[i] != r.target[i - 1];
  EXPECT_EQ(changes, 0);
}

TEST(Corrector, FlexLeavesSmallDeviations) {
  CorrectorParams p = correctionPreset(CorrectionPreset::Robot);
  p.flexCents = 25.0f;
  const auto small = run(p, [](double) { return 69.15; }, 0.3);
  EXPECT_NEAR(small.out.back(), 69.15f, 1e-4f);
  const auto big = run(p, [](double) { return 69.35; }, 0.3);
  EXPECT_NEAR(big.out.back(), 69.0f, 0.01f);
}

TEST(Corrector, CorrectionIsClamped) {
  CorrectorParams p = correctionPreset(CorrectionPreset::Robot);
  p.maxCorrectionCents = 20.0f;
  const auto r = run(p, [](double) { return 69.4; }, 0.3);
  EXPECT_NEAR(r.out.back() - 69.4f, -0.2f, 1e-4f);
}

TEST(Corrector, ShortGapKeepsStateLongGapResets) {
  const auto p = correctionPreset(CorrectionPreset::Singer);
  // Nốt 69,3 → lặng 40 ms → nốt 69,3: không phải trượt lại từ đầu.
  auto contour = [](double t) { return (t > 0.3 && t < 0.34) ? std::nan("") : 69.3; };
  const auto r = run(p, contour, 0.5);
  const auto after = static_cast<std::size_t>(0.34 * kFrameRate) + 1;
  EXPECT_NEAR(r.out[after], 69.0f, 0.03f);
  // Lặng 200 ms → bắt đầu lại (khung đầu sau lặng chưa kéo hết).
  auto contour2 = [](double t) { return (t > 0.3 && t < 0.5) ? std::nan("") : 69.3; };
  const auto r2 = run(p, contour2, 0.7);
  const auto after2 = static_cast<std::size_t>(0.5 * kFrameRate);  // khung hữu thanh đầu tiên
  EXPECT_GT(r2.out[after2] - 69.0f, 0.1f);
}

TEST(Corrector, NaturalPreservesVietnameseToneShape) {
  // Đường nét thanh điệu (hỏi, ngã, sắc, huyền) phải còn nhận ra được sau preset Tự nhiên:
  // tương quan hình dạng ≥ 0,9 và biên độ dao động còn ≥ 60%.
  using test::VietTone;
  const auto p = correctionPreset(CorrectionPreset::Natural);
  for (VietTone tone : {VietTone::Huyen, VietTone::Sac, VietTone::Hoi, VietTone::Nga}) {
    const double base = 67.0;  // Sol4
    auto contour = [&](double t) {
      const double ph = std::fmod(t, 0.4) / 0.4;
      const double st = test::vietToneShape(tone, ph);
      return std::isnan(st) ? std::nan("") : base + st;
    };
    const auto r = run(p, contour, 2.0);
    // Đo trên các âm tiết sau âm tiết đầu.
    double sxy = 0, sxx = 0, syy = 0, mx = 0, my = 0;
    int n = 0;
    for (std::size_t i = 100; i < r.in.size(); ++i) {
      if (std::isnan(r.in[i])) continue;
      mx += r.in[i];
      my += r.out[i];
      ++n;
    }
    mx /= n;
    my /= n;
    for (std::size_t i = 100; i < r.in.size(); ++i) {
      if (std::isnan(r.in[i])) continue;
      sxy += (r.in[i] - mx) * (r.out[i] - my);
      sxx += (r.in[i] - mx) * (r.in[i] - mx);
      syy += (r.out[i] - my) * (r.out[i] - my);
    }
    const double corr = sxy / std::sqrt(sxx * syy);
    const double range = std::sqrt(syy / sxx);
    SCOPED_TRACE(testing::Message()
                 << "tone=" << static_cast<int>(tone) << " corr=" << corr << " range=" << range);
    EXPECT_GT(corr, 0.9);
    EXPECT_GT(range, 0.6);
  }
}
