// Dò onset cho giữ transient (M4 B3): đúng thời điểm, không bắt nhầm trên âm ngân / nhiễu / đuôi
// trống.

#include "stretch/transient_detector.h"

#include <gtest/gtest.h>

#include <cmath>

#include "util/stretch_eval.h"

using namespace tonecore;

namespace {

std::vector<double> detect(const test::StretchSignal& sig) {
  TransientDetector det(2, sig.rate, 4096);
  det.start(sig.stereo.data(), sig.frames(), -8192);
  det.ensure(static_cast<int64_t>(sig.frames()));
  std::vector<double> t;
  for (std::size_t i = 0; i < det.count(); ++i) t.push_back(det.onset(i).time / sig.rate);
  return t;
}

}  // namespace

// Trống đứng riêng: đúng 16 tiếng, mỗi tiếng lệch ≤ 0,2 ms; không bắt nhầm trong đuôi kick (thân
// kick quét 150 → 50 Hz làm flux nhô lên ~50 ms sau tiếng đánh).
TEST(TransientDetector, FindsEveryDrumHitExactly) {
  for (const auto& sig : test::stretchSuite()) {
    if (sig.name != "drums") continue;
    const auto got = detect(sig);
    ASSERT_EQ(got.size(), sig.onsets.size());
    for (std::size_t i = 0; i < got.size(); ++i) EXPECT_NEAR(got[i], sig.onsets[i], 0.0002) << i;
  }
}

// Beat có hợp âm + bass ngân: đủ 32 tiếng (mỗi 250 ms), không thêm.
TEST(TransientDetector, BeatWithSustainedChords) {
  for (const auto& sig : test::stretchSuite()) {
    if (sig.name != "beat") continue;
    const auto got = detect(sig);
    ASSERT_EQ(got.size(), 32u);
    for (std::size_t i = 0; i < got.size(); ++i)
      EXPECT_NEAR(got[i], 0.25 * static_cast<double>(i), 0.0005) << i;
  }
}

// Âm ngân và nhiễu đều: chỉ có một onset lúc tín hiệu bắt đầu sau im lặng.
TEST(TransientDetector, NoFalseOnsetsOnSteadySignals) {
  for (const auto& sig : test::stretchSuite()) {
    if (sig.name != "sine" && sig.name != "chord" && sig.name != "harmonic" &&
        sig.name != "noise") {
      continue;
    }
    const auto got = detect(sig);
    ASSERT_EQ(got.size(), 1u) << sig.name;
    EXPECT_NEAR(got[0], 0.0, 0.001) << sig.name;
  }
}
