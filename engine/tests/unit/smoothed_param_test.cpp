#include "core/smoothed_param.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using tonecore::SmoothedParam;

namespace {
constexpr double kFs = 48000.0;
constexpr double kTau = 0.01;  // 10 ms
const int kTauSamples = static_cast<int>(kTau * kFs);
}  // namespace

TEST(SmoothedParam, StartsAtInitialValue) {
  SmoothedParam p(0.5f, 0.0f, 1.0f);
  p.prepare(kFs, kTau);
  EXPECT_FLOAT_EQ(p.next(), 0.5f);
  EXPECT_TRUE(p.isSettled());
}

TEST(SmoothedParam, ReachesAbout63PercentAfterOneTimeConstant) {
  SmoothedParam p(0.0f, 0.0f, 1.0f);
  p.prepare(kFs, kTau);
  p.setTarget(1.0f);
  float v = 0.0f;
  for (int i = 0; i < kTauSamples; ++i) v = p.next();
  // Đáp ứng bậc thang của bộ lọc một cực: 1 - e^-1 ≈ 0.632 sau τ.
  EXPECT_NEAR(v, 1.0f - std::exp(-1.0f), 0.01f);
}

TEST(SmoothedParam, SettlesExactlyOnTarget) {
  SmoothedParam p(0.0f, 0.0f, 1.0f);
  p.prepare(kFs, kTau);
  p.setTarget(0.8f);
  for (int i = 0; i < kTauSamples * 20; ++i) p.next();
  EXPECT_EQ(p.current(), 0.8f);
  EXPECT_TRUE(p.isSettled());
}

TEST(SmoothedParam, IsMonotonicTowardTarget) {
  SmoothedParam p(1.0f, 0.0f, 1.0f);
  p.prepare(kFs, kTau);
  p.setTarget(0.0f);
  float prev = 1.0f;
  for (int i = 0; i < kTauSamples * 5; ++i) {
    const float v = p.next();
    ASSERT_LE(v, prev);
    prev = v;
  }
}

TEST(SmoothedParam, ClampsToRangeAndRejectsNaN) {
  SmoothedParam p(0.5f, 0.0f, 2.0f);
  p.setTarget(5.0f);
  EXPECT_EQ(p.target(), 2.0f);
  p.setTarget(-1.0f);
  EXPECT_EQ(p.target(), 0.0f);
  p.setTarget(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(p.target(), 0.0f);
}

TEST(SmoothedParam, ZeroTimeConstantJumpsImmediately) {
  SmoothedParam p(0.0f, 0.0f, 1.0f);
  p.prepare(kFs, 0.0);
  p.setTarget(1.0f);
  EXPECT_EQ(p.next(), 1.0f);
}
