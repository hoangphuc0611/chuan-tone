// Limiter đỉnh cho beat đã đổi tone (M4 B4): đỉnh vượt 0 dBFS sau đổi pha → không còn mẫu bị cắt.

#include "stretch/peak_limiter.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace tonecore;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> stereoSine(double hz, double amp, std::size_t frames) {
  std::vector<float> x(2 * frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const auto v =
        static_cast<float>(amp * std::sin(2 * kPi * hz * static_cast<double>(i) / kRate));
    x[2 * i] = v;
    x[2 * i + 1] = 0.5f * v;
  }
  return x;
}

}  // namespace

// Dưới trần: đi thẳng, đúng từng mẫu (chỉ trễ lookahead).
TEST(PeakLimiter, TransparentBelowCeiling) {
  PeakLimiter lim(2, kRate);
  const auto x = stereoSine(440.0, 0.9, 4800);
  std::vector<float> y(x.size());
  lim.process(x.data(), y.data(), 4800);
  const auto L = static_cast<std::size_t>(lim.lookahead());
  for (std::size_t i = 0; i < 2 * L; ++i) EXPECT_EQ(y[i], 0.0f);
  for (std::size_t i = 2 * L; i < y.size(); ++i) ASSERT_EQ(y[i], x[i - 2 * L]) << i;
}

// Đỉnh ngắn +4 dB: ra không vượt trần (≤ 1 % sai số), hai kênh cùng hệ số, hết đỉnh thì hồi lại.
TEST(PeakLimiter, CatchesShortOversAndRecovers) {
  auto x = stereoSine(440.0, 0.5, 48000);
  for (std::size_t i = 24000; i < 24000 + 96; ++i) {  // 2 ms vượt 1,58 (+4 dB)
    x[2 * i] *= 3.17f;
    x[2 * i + 1] *= 3.17f;
  }
  std::vector<float> y = x;
  limitPeaks(y, 2, kRate);
  float peak = 0.0f;
  for (float v : y) peak = std::max(peak, std::abs(v));
  EXPECT_LE(peak, 0.98f * 1.01f);
  // Tỉ lệ L/R giữ nguyên (cùng hệ số cho hai kênh) ngay trong đoạn bị giới hạn.
  for (std::size_t i = 24000; i < 24096; ++i) {
    if (std::abs(x[2 * i]) > 0.1f) EXPECT_NEAR(y[2 * i + 1] / y[2 * i], 0.5f, 1e-4f) << i;
  }
  // 0,5 s sau đỉnh: đã hồi về gần như đi thẳng (release τ 60 ms).
  for (std::size_t i = 48000 - 4800; i < 48000; ++i) EXPECT_NEAR(y[2 * i], x[2 * i], 1e-3f) << i;
}
