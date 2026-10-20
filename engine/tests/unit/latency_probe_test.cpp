#include "latency/latency_probe.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using tonecore::LatencyProbe;

namespace {

constexpr uint32_t kFs = 48000;

// Cấu hình nhỏ để test chạy nhanh ở bản Debug/sanitizer.
LatencyProbe::Config smallConfig() {
  LatencyProbe::Config c;
  c.mlsOrder = 12;
  c.repeats = 5;
  c.maxLatencySec = 0.1;
  c.prerollSec = 0.02;
  return c;
}

struct Loopback {
  uint32_t delay = 0;        // độ trễ round-trip giả lập (mẫu)
  float gain = 0.05f;        // suy hao loa → mic
  float reflection = 0.3f;   // tiếng vọng phòng, trễ thêm 300 mẫu
  float noiseRms = 0.0079f;  // ≈ SNR 10 dB so với tiếng vọng chính
  float dc = 0.01f;
  bool connected = true;  // false = mic không nghe thấy loa
};

// Chạy toàn bộ một lần đo qua vòng loopback giả lập, theo từng block như audio callback thật.
LatencyProbe::Result runProbe(LatencyProbe& probe, const Loopback& lb, uint32_t block) {
  std::vector<float> played;  // toàn bộ tín hiệu đã phát
  std::mt19937 rng(1234);
  std::normal_distribution<float> noise(0.0f, lb.noiseRms);
  std::vector<float> mic(block);
  std::vector<float> stim(block);

  probe.arm();
  int guard = 0;
  while (!probe.isComplete() && guard++ < 100000) {
    const auto base = static_cast<int64_t>(played.size());
    for (uint32_t i = 0; i < block; ++i) {
      const int64_t t = base + i;
      float x = lb.dc + noise(rng);
      if (lb.connected) {
        const int64_t t1 = t - lb.delay;
        const int64_t t2 = t1 - 300;
        if (t1 >= 0 && t1 < base) x += lb.gain * played[static_cast<size_t>(t1)];
        if (t2 >= 0 && t2 < base) x += lb.gain * lb.reflection * played[static_cast<size_t>(t2)];
      }
      mic[i] = x;
    }
    probe.processBlock(mic.data(), stim.data(), block);
    played.insert(played.end(), stim.begin(), stim.end());
  }
  EXPECT_TRUE(probe.isComplete());
  return probe.analyze();
}

}  // namespace

// Độ trễ < một block: tiếng vọng trong cùng block chưa "phát" ra nên mô phỏng chỉ hỗ trợ
// delay ≥ block. Thực tế độ trễ round-trip luôn ≥ 2 block.
class LatencyProbeDelayTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(LatencyProbeDelayTest, MeasuresKnownDelayWithinOneFrame) {
  LatencyProbe probe(kFs, smallConfig());
  Loopback lb;
  lb.delay = GetParam();
  const auto r = runProbe(probe, lb, 192);
  ASSERT_TRUE(r.valid) << "validBursts=" << r.validBursts << " spread=" << r.spreadFrames;
  EXPECT_NEAR(r.latencyFrames, lb.delay, 1.0);
  EXPECT_NEAR(r.latencyMs, 1000.0 * lb.delay / kFs, 0.03);
  EXPECT_EQ(r.validBursts, 5);
  EXPECT_GT(r.confidence, 0.5f);
}

INSTANTIATE_TEST_SUITE_P(Delays, LatencyProbeDelayTest, ::testing::Values(192u, 517u, 2400u));

TEST(LatencyProbe, WorksWithOddBlockSize) {
  LatencyProbe probe(kFs, smallConfig());
  Loopback lb;
  lb.delay = 1111;
  const auto r = runProbe(probe, lb, 97);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.latencyFrames, 1111.0, 1.0);
}

TEST(LatencyProbe, ReportsInvalidWhenMicHearsNothing) {
  LatencyProbe probe(kFs, smallConfig());
  Loopback lb;
  lb.connected = false;
  const auto r = runProbe(probe, lb, 192);
  EXPECT_FALSE(r.valid);
}

TEST(LatencyProbe, IdleProbeOutputsSilence) {
  LatencyProbe probe(kFs, smallConfig());
  std::vector<float> mic(64, 0.3f);
  std::vector<float> stim(64, 1.0f);
  EXPECT_FALSE(probe.processBlock(mic.data(), stim.data(), 64));
  for (float v : stim) EXPECT_EQ(v, 0.0f);
  EXPECT_FALSE(probe.isActive());
  EXPECT_FALSE(probe.analyze().valid);  // chưa đo xong
}

TEST(LatencyProbe, CancelStopsMeasurement) {
  LatencyProbe probe(kFs, smallConfig());
  std::vector<float> mic(256, 0.0f);
  std::vector<float> stim(256);
  probe.arm();
  EXPECT_TRUE(probe.isActive());
  EXPECT_TRUE(probe.processBlock(mic.data(), stim.data(), 256));
  probe.cancel();
  EXPECT_FALSE(probe.processBlock(mic.data(), stim.data(), 256));
  EXPECT_FALSE(probe.isActive());
  EXPECT_FALSE(probe.isComplete());
}

TEST(LatencyProbe, StimulusLevelIsBounded) {
  LatencyProbe::Config c = smallConfig();
  c.level = 0.25f;
  LatencyProbe probe(kFs, c);
  std::vector<float> stim(512);
  float peak = 0.0f;
  probe.arm();
  while (!probe.isComplete()) {
    probe.processBlock(nullptr, stim.data(), 512);
    for (float v : stim) peak = std::max(peak, std::fabs(v));
  }
  EXPECT_FLOAT_EQ(peak, 0.25f);
}
