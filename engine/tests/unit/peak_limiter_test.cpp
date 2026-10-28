// Limiter đỉnh cho beat đã đổi tone (M4 B4): đỉnh vượt 0 dBFS sau đổi pha → không còn mẫu bị cắt.

#include "fx/peak_limiter.h"

#include <gtest/gtest.h>

#include <algorithm>
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

// Cực tiểu cửa sổ trượt bằng hàng đợi đơn điệu (O(1)) cho ra đúng từng mẫu như quét lại cả cửa sổ
// mỗi mẫu (cách làm ban đầu), trên tín hiệu ngẫu nhiên thường xuyên vượt trần, chia khối lẻ.
TEST(PeakLimiter, MatchesBruteForceWindowMinimum) {
  constexpr int kCh = 2;
  constexpr double kFs = 48000.0;
  const std::size_t frames = 20000;
  std::vector<float> x(kCh * frames);
  uint32_t state = 5;
  for (float& v : x) {
    state = state * 1664525u + 1013904223u;
    v = 1.6f * (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
  }
  PeakLimiter fast(kCh, kFs, 0.98f, 1.0, 60.0);
  std::vector<float> y(x.size());
  for (std::size_t i = 0; i < frames;) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(37 + i % 200, frames - i));
    fast.process(x.data() + kCh * i, y.data() + kCh * i, n);
    i += n;
  }
  // Tham chiếu: như PeakLimiter, nhưng min quét lại L + 1 giá trị gần nhất mỗi mẫu.
  const int L = fast.lookahead();
  const auto attack = static_cast<float>(1.0 - std::pow(0.01, 1.0 / L));
  const auto release = static_cast<float>(1.0 - std::exp(-1.0 / (60.0 * 0.001 * kFs)));
  std::vector<float> need(frames);
  float gain = 1.0f;
  for (std::size_t n = 0; n < frames; ++n) {
    const float peak = std::max(std::fabs(x[kCh * n]), std::fabs(x[kCh * n + 1]));
    need[n] = peak > 0.98f ? 0.98f / peak : 1.0f;
    float target = 1.0f;
    for (std::size_t k = n >= static_cast<std::size_t>(L) ? n - L : 0; k <= n; ++k) {
      target = std::min(target, need[k]);
    }
    gain += (target - gain) * (target < gain ? attack : release);
    for (int c = 0; c < kCh; ++c) {
      const float in = n >= static_cast<std::size_t>(L) ? x[kCh * (n - L) + c] : 0.0f;
      ASSERT_EQ(y[kCh * n + c], std::clamp(in * gain, -1.0f, 1.0f)) << n;
    }
  }
}
