// Engine M5 qua C API: limiter đầu ra thay chỗ kẹp ±1.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"

namespace {

constexpr uint32_t kBlock = 256;
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

}  // namespace

// Beat sát 0 dBFS + giọng nghe lại to + master ×2: tổng tới ~3,8 nhưng đầu ra không mẫu nào vượt
// trần 0,98 (+1%) và không bị kẹp ±1; vẫn to (limiter chỉ hạ đỉnh). Trước M5 tầng ra kẹp ±1 → méo.
TEST(EngineLimiter, LoudMixStaysBelowCeiling) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const std::string path = tonecore::test::tempPath("m5_loud_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(
      path, tonecore::test::sineInt16(220.0, 48000, 48000 * 3, 2, 0.95), 48000, 2));
  ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
  for (int i = 0; i < 3000; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    bool loaded = false;
    for (int32_t k = 0; k < n; ++k) loaded |= ev[k].type == TC_EVENT_BEAT_LOADED;
    if (loaded) break;
    std::vector<float> idle(2 * kBlock);
    tc_engine_process_offline(e, nullptr, idle.data(), kBlock);
  }
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MASTER, TC_PARAM_GAIN, 2.0f), TC_OK);
  ASSERT_EQ(tc_play(e), TC_OK);
  std::vector<float> in(kBlock), out(2 * kBlock);
  float peak = 0.0f;
  int atUnity = 0;
  uint64_t t = 0;
  for (int b = 0; b < 300; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i, ++t) {
      in[i] =
          static_cast<float>(0.9 * std::sin(2.0 * kPi * 330.0 * static_cast<double>(t) / kRate));
    }
    ASSERT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
    if (b < 20) continue;  // bỏ lúc gain đang trượt
    for (const float v : out) {
      peak = std::max(peak, std::fabs(v));
      atUnity += std::fabs(v) >= 1.0f;
    }
  }
  EXPECT_LE(peak, 0.98f * 1.01f);
  EXPECT_GT(peak, 0.9f);
  EXPECT_EQ(atUnity, 0);
  tc_engine_destroy(e);
}
