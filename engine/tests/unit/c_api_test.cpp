// Kiểm tra C API đúng như Dart FFI sẽ gọi.

#include "tonecore/tonecore.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct EngineHolder {
  tc_engine* e = nullptr;
  ~EngineHolder() { tc_engine_destroy(e); }
};

}  // namespace

TEST(CApi, VersionInfo) {
  EXPECT_EQ(tc_get_api_version(), static_cast<uint32_t>(TC_API_VERSION));
  ASSERT_NE(tc_get_version_string(), nullptr);
  EXPECT_FALSE(std::string(tc_get_version_string()).empty());
  EXPECT_STREQ(tc_result_to_string(TC_OK), "TC_OK");
  EXPECT_STREQ(tc_result_to_string(TC_ERR_INVALID_STATE), "TC_ERR_INVALID_STATE");
}

TEST(CApi, ConfigInitSetsDefaults) {
  tc_engine_config c;
  std::memset(&c, 0xAB, sizeof(c));
  tc_engine_config_init(&c);
  EXPECT_EQ(c.struct_size, sizeof(tc_engine_config));
  EXPECT_EQ(c.sample_rate, 48000u);
  EXPECT_EQ(c.input_channels, 1u);
  EXPECT_EQ(c.output_channels, 2u);
  EXPECT_EQ(c.backend, TC_BACKEND_NULL);
}

TEST(CApi, CreateWithDefaultsAndDestroy) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  ASSERT_NE(h.e, nullptr);
}

TEST(CApi, CreateRejectsBadArguments) {
  EXPECT_EQ(tc_engine_create(nullptr, nullptr), TC_ERR_INVALID_ARG);

  tc_engine* e = reinterpret_cast<tc_engine*>(0x1);
  tc_engine_config c;
  tc_engine_config_init(&c);
  c.sample_rate = 100;
  EXPECT_EQ(tc_engine_create(&c, &e), TC_ERR_INVALID_ARG);
  EXPECT_EQ(e, nullptr);  // luôn đặt NULL khi lỗi

  tc_engine_config_init(&c);
  c.struct_size = 4;
  EXPECT_EQ(tc_engine_create(&c, &e), TC_ERR_INVALID_ARG);

  tc_engine_config_init(&c);
  c.output_channels = 0;
  EXPECT_EQ(tc_engine_create(&c, &e), TC_ERR_INVALID_ARG);
}

TEST(CApi, PlatformBackendNotYetSupported) {
  tc_engine_config c;
  tc_engine_config_init(&c);
  c.backend = TC_BACKEND_PLATFORM;
  tc_engine* e = nullptr;
  EXPECT_EQ(tc_engine_create(&c, &e), TC_ERR_NOT_SUPPORTED);
}

TEST(CApi, NullEngineIsHandled) {
  tc_engine_destroy(nullptr);
  EXPECT_EQ(tc_engine_start(nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_engine_stop(nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_param(nullptr, TC_NODE_MASTER, TC_PARAM_GAIN, 1.0f), TC_ERR_INVALID_ARG);
}

TEST(CApi, StartStopStateMachine) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  EXPECT_EQ(tc_engine_stop(h.e), TC_ERR_INVALID_STATE);
  EXPECT_EQ(tc_engine_start(h.e), TC_OK);
  EXPECT_EQ(tc_engine_start(h.e), TC_ERR_INVALID_STATE);
  EXPECT_EQ(tc_engine_stop(h.e), TC_OK);
}

TEST(CApi, ProcessRequiresRunningState) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  std::vector<float> out(2 * 64);
  EXPECT_EQ(tc_engine_process_offline(h.e, nullptr, out.data(), 64), TC_ERR_INVALID_STATE);
}

TEST(CApi, ProcessCopiesMonoInputToStereoWithGain) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  ASSERT_EQ(tc_engine_start(h.e), TC_OK);

  constexpr uint32_t kFrames = 256;
  std::vector<float> in(kFrames, 0.5f);
  std::vector<float> out(kFrames * 2, -1.0f);

  // Gain mặc định 1.0 → đầu ra = đầu vào ở cả hai kênh.
  ASSERT_EQ(tc_engine_process_offline(h.e, in.data(), out.data(), kFrames), TC_OK);
  for (uint32_t i = 0; i < kFrames; ++i) {
    EXPECT_FLOAT_EQ(out[2 * i], 0.5f);
    EXPECT_FLOAT_EQ(out[2 * i + 1], 0.5f);
  }

  // Đổi gain → đầu ra tiến dần (làm mượt), sau ~0.5 s phải đạt đúng đích.
  ASSERT_EQ(tc_set_param(h.e, TC_NODE_MASTER, TC_PARAM_GAIN, 2.0f), TC_OK);
  float g = 0.0f;
  ASSERT_EQ(tc_get_param(h.e, TC_NODE_MASTER, TC_PARAM_GAIN, &g), TC_OK);
  EXPECT_EQ(g, 2.0f);

  ASSERT_EQ(tc_engine_process_offline(h.e, in.data(), out.data(), kFrames), TC_OK);
  EXPECT_GT(out[0], 0.5f);                    // đã bắt đầu tăng
  EXPECT_LT(out[0], 1.0f);                    // nhưng chưa nhảy thẳng tới đích
  EXPECT_GT(out[2 * (kFrames - 1)], out[0]);  // tăng dần trong block

  for (int b = 0; b < 100; ++b) {
    ASSERT_EQ(tc_engine_process_offline(h.e, in.data(), out.data(), kFrames), TC_OK);
  }
  EXPECT_FLOAT_EQ(out[0], 1.0f);
  EXPECT_FLOAT_EQ(out[1], 1.0f);
}

TEST(CApi, ProcessWithNullInputOutputsSilence) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  ASSERT_EQ(tc_engine_start(h.e), TC_OK);
  std::vector<float> out(2 * 32, 1.0f);
  ASSERT_EQ(tc_engine_process_offline(h.e, nullptr, out.data(), 32), TC_OK);
  for (float v : out) EXPECT_EQ(v, 0.0f);
}

TEST(CApi, ProcessRejectsOversizedBlock) {
  EngineHolder h;
  tc_engine_config c;
  tc_engine_config_init(&c);
  c.max_block_frames = 128;
  ASSERT_EQ(tc_engine_create(&c, &h.e), TC_OK);
  ASSERT_EQ(tc_engine_start(h.e), TC_OK);
  std::vector<float> out(2 * 256);
  EXPECT_EQ(tc_engine_process_offline(h.e, nullptr, out.data(), 256), TC_ERR_INVALID_ARG);
}

TEST(CApi, UnknownParamIsRejected) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  EXPECT_EQ(tc_set_param(h.e, 99, TC_PARAM_GAIN, 1.0f), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_param(h.e, TC_NODE_MASTER, 99, 1.0f), TC_ERR_INVALID_ARG);
}

TEST(CApi, MetricsCountProcessedFrames) {
  EngineHolder h;
  ASSERT_EQ(tc_engine_create(nullptr, &h.e), TC_OK);
  ASSERT_EQ(tc_engine_start(h.e), TC_OK);
  std::vector<float> out(2 * 480);
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(tc_engine_process_offline(h.e, nullptr, out.data(), 480), TC_OK);
  }

  tc_metrics m{};
  EXPECT_EQ(tc_get_metrics(h.e, &m), TC_ERR_INVALID_ARG);  // thiếu struct_size
  m.struct_size = sizeof(tc_metrics);
  ASSERT_EQ(tc_get_metrics(h.e, &m), TC_OK);
  EXPECT_EQ(m.state, TC_STATE_RUNNING);
  EXPECT_EQ(m.sample_rate, 48000u);
  EXPECT_EQ(m.frames_processed, 4800u);
  EXPECT_EQ(m.callbacks, 10u);
  EXPECT_EQ(m.xruns, 0u);
  EXPECT_GE(m.cpu_load, 0.0f);
}
