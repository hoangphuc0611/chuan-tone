// Engine 0.9 (M8 B4, docs/m8-plan.md mục 3): mức micro (đỉnh ~300 ms, số lần chạm 0 dBFS) và chọn
// micro (lệnh tới backend; backend giả thay cho RemoteIO / Oboe).

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "core/engine.h"
#include "io/audio_backend.h"
#include "tonecore/tonecore.h"

namespace {

constexpr uint32_t kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

tc_engine* makeEngine(tc_backend backend = TC_BACKEND_NULL) {
  tc_engine_config cfg;
  tc_engine_config_init(&cfg);
  cfg.max_block_frames = kBlock;
  cfg.backend = backend;
  tc_engine* e = nullptr;
  EXPECT_EQ(tc_engine_create(&cfg, &e), TC_OK);
  return e;
}

tc_metrics metrics(tc_engine* e) {
  tc_metrics m{};
  m.struct_size = sizeof(m);
  EXPECT_EQ(tc_get_metrics(e, &m), TC_OK);
  return m;
}

// Chạy [blocks] khối micro do [gen](chỉ số mẫu) tạo.
template <typename Gen>
void run(tc_engine* e, int blocks, Gen gen, uint64_t& n) {
  std::vector<float> in(kBlock);
  std::vector<float> out(2 * kBlock);
  for (int b = 0; b < blocks; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i) in[i] = gen(n++);
    ASSERT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
  }
}

}  // namespace

// Sin biên độ 0,5 → đỉnh −6,02 dBFS; im lặng hơn 600 ms → về −120; chưa chạm 0 dBFS lần nào.
TEST(EngineInputMeter, PeakFollowsMicAndDecays) {
  tc_engine* e = makeEngine();
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  EXPECT_FLOAT_EQ(metrics(e).input_peak_db, -120.0f);
  uint64_t n = 0;
  run(
      e, 40,
      [](uint64_t i) {
        return static_cast<float>(0.5 *
                                  std::sin(2 * kPi * 440.0 * static_cast<double>(i) / 48000.0));
      },
      n);
  tc_metrics m = metrics(e);
  EXPECT_NEAR(m.input_peak_db, -6.02f, 0.05f);
  EXPECT_EQ(m.input_clips, 0u);
  // Vừa im lặng 100 ms: vẫn thấy đỉnh của cửa sổ trước.
  run(e, 19, [](uint64_t) { return 0.0f; }, n);
  EXPECT_NEAR(metrics(e).input_peak_db, -6.02f, 0.05f);
  // Im lặng ~700 ms: hai cửa sổ đã qua → về sàn.
  run(e, 130, [](uint64_t) { return 0.0f; }, n);
  EXPECT_FLOAT_EQ(metrics(e).input_peak_db, -120.0f);
  tc_engine_destroy(e);
}

// Mỗi đợt mẫu chạm 0 dBFS tính một lần, kể cả đợt kéo dài qua ranh giới khối; −0,1 dBFS không tính.
TEST(EngineInputMeter, CountsClipRuns) {
  tc_engine* e = makeEngine();
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  uint64_t n = 0;
  // 3 khối, mỗi khối một đợt 10 mẫu ở +1 rồi một đợt 5 mẫu ở −1.
  run(
      e, 3,
      [](uint64_t i) {
        const uint64_t k = i % kBlock;
        if (k >= 100 && k < 110) return 1.0f;
        if (k >= 200 && k < 205) return -1.0f;
        return 0.1f;
      },
      n);
  EXPECT_EQ(metrics(e).input_clips, 6u);
  // Một đợt dài 600 mẫu (qua 3 ranh giới khối) → +1.
  n = 0;
  run(e, 4, [](uint64_t i) { return i >= 50 && i < 650 ? 0.9995f : 0.0f; }, n);
  EXPECT_EQ(metrics(e).input_clips, 7u);
  // −0,1 dBFS: to nhưng chưa chạm.
  run(e, 4, [](uint64_t) { return 0.9886f; }, n);
  tc_metrics m = metrics(e);
  EXPECT_EQ(m.input_clips, 7u);
  EXPECT_NEAR(m.input_peak_db, 0.0f, 0.11f);
  tc_engine_destroy(e);
}

// Binding v0.8 (struct kết thúc ở fx_chains) vẫn đọc được; phần sau không bị ghi.
TEST(EngineInputMeter, OldMetricsStructStillAccepted) {
  tc_engine* e = makeEngine();
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  tc_metrics m{};
  m.input_clips = 12345u;
  m.struct_size = offsetof(tc_metrics, input_peak_db);
  EXPECT_EQ(tc_get_metrics(e, &m), TC_OK);
  EXPECT_EQ(m.state, TC_STATE_RUNNING);
  EXPECT_EQ(m.input_clips, 12345u);
  tc_engine_destroy(e);
}

TEST(EngineInputPreference, ValidatesAndReports) {
  tc_engine* e = makeEngine();
  EXPECT_EQ(metrics(e).input_preference, TC_INPUT_AUTO);
  for (int32_t p : {TC_INPUT_BUILTIN, TC_INPUT_HEADSET, TC_INPUT_USB, TC_INPUT_AUTO}) {
    EXPECT_EQ(tc_set_input_preference(e, p, 0), TC_OK);
    EXPECT_EQ(metrics(e).input_preference, p);
  }
  EXPECT_EQ(tc_set_input_preference(e, TC_INPUT_BLUETOOTH, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_input_preference(e, -1, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_input_preference(e, 9, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_input_preference(e, TC_INPUT_USB, -3), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_input_preference(nullptr, TC_INPUT_USB, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(metrics(e).input_preference, TC_INPUT_AUTO);
  // Backend NULL: không có micro thật → chưa rõ.
  EXPECT_EQ(metrics(e).input_source, TC_INPUT_AUTO);
  tc_engine_destroy(e);
}

namespace {

// Backend giả: ghi lại các lệnh chọn micro, báo micro đang dùng theo lệnh cuối.
struct FakeLog {
  std::vector<std::pair<int32_t, int32_t>> prefs;  // (preference, deviceId)
  int startedAfterPrefs = -1;                      // số lệnh đã nhận lúc start()
  bool started = false;
};
FakeLog* g_log = nullptr;

class FakeBackend final : public tonecore::AudioBackend {
 public:
  tc_result start() override {
    g_log->started = true;
    g_log->startedAfterPrefs = static_cast<int>(g_log->prefs.size());
    return TC_OK;
  }
  void stop() override { g_log->started = false; }
  tonecore::BackendInfo info() const override {
    tonecore::BackendInfo i;
    i.sampleRate = 48000;
    i.inputSource = g_log->prefs.empty() ? TC_INPUT_AUTO : g_log->prefs.back().first;
    return i;
  }
  uint32_t xruns() const override { return 0; }
  void setInputPreference(int32_t preference, int32_t deviceId) override {
    g_log->prefs.emplace_back(preference, deviceId);
  }
};

std::unique_ptr<tonecore::AudioBackend> makeFake(tonecore::BackendHost&,
                                                 const tonecore::EngineConfig&) {
  return std::make_unique<FakeBackend>();
}

}  // namespace

// Lựa chọn đặt trước khi start được báo cho backend trước khi mở stream; đổi khi đang chạy → báo
// ngay; micro đang dùng lấy từ backend.
TEST(EngineInputPreference, ReachesBackend) {
  FakeLog log;
  g_log = &log;
  tonecore::setBackendFactoryForTesting(&makeFake);
  tc_engine* e = makeEngine(TC_BACKEND_PLATFORM);
  ASSERT_EQ(tc_set_input_preference(e, TC_INPUT_HEADSET, 0), TC_OK);
  EXPECT_TRUE(log.prefs.empty());  // chưa có backend
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(log.prefs.size(), 1u);
  EXPECT_EQ(log.prefs[0], std::make_pair(TC_INPUT_HEADSET, 0));
  EXPECT_EQ(log.startedAfterPrefs, 1);
  EXPECT_EQ(metrics(e).input_source, TC_INPUT_HEADSET);

  ASSERT_EQ(tc_set_input_preference(e, TC_INPUT_USB, 7), TC_OK);
  ASSERT_EQ(log.prefs.size(), 2u);
  EXPECT_EQ(log.prefs[1], std::make_pair(TC_INPUT_USB, 7));
  tc_metrics m = metrics(e);
  EXPECT_EQ(m.input_source, TC_INPUT_USB);
  EXPECT_EQ(m.input_preference, TC_INPUT_USB);

  // Sai thì không tới backend.
  EXPECT_EQ(tc_set_input_preference(e, TC_INPUT_BLUETOOTH, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(log.prefs.size(), 2u);

  // Dừng rồi chạy lại: backend mới nhận lại lựa chọn cuối trước khi mở.
  ASSERT_EQ(tc_engine_stop(e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(log.prefs.size(), 3u);
  EXPECT_EQ(log.prefs[2], std::make_pair(TC_INPUT_USB, 7));
  EXPECT_EQ(log.startedAfterPrefs, 3);
  tc_engine_destroy(e);
  tonecore::setBackendFactoryForTesting(nullptr);
  g_log = nullptr;
}
