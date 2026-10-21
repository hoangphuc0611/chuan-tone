// Test engine M1 qua C API: bộ trộn, route, beat, sự kiện, đo độ trễ.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"

namespace {

constexpr uint32_t kBlock = 256;

// Đỉnh của kênh `ch` trong block stereo interleaved.
float peakOf(const std::vector<float>& out, int ch) {
  float p = 0.0f;
  for (std::size_t i = 0; i < out.size() / 2; ++i) p = std::max(p, std::fabs(out[2 * i + ch]));
  return p;
}

struct Fixture {
  tc_engine* e = nullptr;
  explicit Fixture(uint32_t maxBlock = 1024) {
    tc_engine_config c;
    tc_engine_config_init(&c);
    c.max_block_frames = maxBlock;
    EXPECT_EQ(tc_engine_create(&c, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
  }
  ~Fixture() { tc_engine_destroy(e); }

  // Chạy n block với micro là sin 1 kHz biên độ `micLevel` (đường micro có lọc thông cao
  // 80 Hz nên không dùng DC), trả block cuối.
  std::vector<float> run(int blocks, float micLevel = 0.0f) {
    std::vector<float> in(kBlock);
    std::vector<float> out(2 * kBlock);
    for (int b = 0; b < blocks; ++b) {
      for (uint32_t i = 0; i < kBlock; ++i) {
        in[i] = micLevel * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                                       static_cast<double>(phase_++) / 48000.0));
      }
      EXPECT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
    }
    return out;
  }
  uint64_t phase_ = 0;

  std::vector<tc_event> poll() {
    tc_event buf[32];
    const int32_t n = tc_poll_events(e, buf, 32);
    EXPECT_GE(n, 0);
    return std::vector<tc_event>(buf, buf + (n > 0 ? n : 0));
  }

  // Chờ sự kiện `type` (tối đa 60 s – phân tích ở bản Debug/ASan có thể chậm).
  bool waitFor(int32_t type, tc_event* found) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
      for (const auto& ev : poll()) {
        if (ev.type == type) {
          if (found != nullptr) *found = ev;
          return true;
        }
      }
      run(1);  // giữ audio "chạy" như thật
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  tc_playback_state playback() {
    tc_playback_state s{};
    s.struct_size = sizeof(s);
    EXPECT_EQ(tc_get_playback_state(e, &s), TC_OK);
    return s;
  }
};

bool hasEvent(const std::vector<tc_event>& evs, int32_t type, int32_t code = -999) {
  for (const auto& e : evs) {
    if (e.type == type && (code == -999 || e.code == code)) return true;
  }
  return false;
}

}  // namespace

TEST(EngineM1, MonitorMixesVoiceWithGain) {
  Fixture f;
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_GAIN, 0.5f), TC_OK);
  const auto out = f.run(40, 0.4f);
  EXPECT_NEAR(peakOf(out, 0), 0.2f, 0.003f);
  EXPECT_NEAR(peakOf(out, 1), 0.2f, 0.003f);
}

TEST(EngineM1, OutputIsClippedToUnity) {
  Fixture f;
  tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f);
  tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_GAIN, 4.0f);
  const auto out = f.run(40, 0.9f);
  EXPECT_EQ(peakOf(out, 0), 1.0f);
}

TEST(EngineM1, BluetoothRouteBlocksMonitorAndNotifies) {
  Fixture f;
  tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f);
  ASSERT_EQ(tc_set_route_hint(f.e, TC_ROUTE_BLUETOOTH), TC_OK);
  const auto evs = f.poll();
  EXPECT_TRUE(hasEvent(evs, TC_EVENT_ROUTE_CHANGED, TC_ROUTE_BLUETOOTH));
  EXPECT_TRUE(hasEvent(evs, TC_EVENT_MONITOR_BLOCKED));

  const auto out = f.run(40, 0.5f);
  EXPECT_EQ(peakOf(out, 0), 0.0f);  // giọng không ra tai nghe Bluetooth

  tc_metrics m{};
  m.struct_size = sizeof(m);
  ASSERT_EQ(tc_get_metrics(f.e, &m), TC_OK);
  EXPECT_EQ(m.route, TC_ROUTE_BLUETOOTH);
  EXPECT_EQ(m.monitor_active, 0);

  // Cắm tai nghe có dây → nghe lại hoạt động trở lại.
  ASSERT_EQ(tc_set_route_hint(f.e, TC_ROUTE_WIRED), TC_OK);
  const auto out2 = f.run(40, 0.5f);
  EXPECT_NEAR(peakOf(out2, 0), 0.5f, 0.005f);
  EXPECT_EQ(tc_set_route_hint(f.e, 42), TC_ERR_INVALID_ARG);
}

TEST(EngineM1, LoadPlayPauseSeekAndEnd) {
  // Beat 0,5 s, 44,1 kHz, stereo: trái = sin 440 Hz, phải = im lặng.
  std::vector<int16_t> pcm(22050 * 2);
  const auto left = tonecore::test::sineInt16(440.0, 44100, 22050, 1);
  for (std::size_t i = 0; i < 22050; ++i) pcm[2 * i] = left[i];
  const std::string path = tonecore::test::tempPath("beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(path, pcm, 44100, 2));

  Fixture f;
  EXPECT_EQ(tc_play(f.e), TC_ERR_INVALID_STATE);  // chưa có beat
  ASSERT_EQ(tc_load_beat(f.e, path.c_str()), TC_OK);
  tc_event ev{};
  ASSERT_TRUE(f.waitFor(TC_EVENT_BEAT_LOADED, &ev));
  EXPECT_EQ(ev.code, TC_OK);
  EXPECT_NEAR(ev.value, 500.0, 0.1);
  auto pb = f.playback();
  EXPECT_EQ(pb.has_beat, 1);
  EXPECT_EQ(pb.playing, 0);
  EXPECT_NEAR(pb.duration_ms, 500.0, 0.1);

  // Phát: kênh trái có tín hiệu, kênh phải im lặng.
  ASSERT_EQ(tc_play(f.e), TC_OK);
  auto out = f.run(10);
  float peakL = 0.0f;
  float peakR = 0.0f;
  for (uint32_t i = 0; i < kBlock; ++i) {
    peakL = std::max(peakL, std::fabs(out[2 * i]));
    peakR = std::max(peakR, std::fabs(out[2 * i + 1]));
  }
  EXPECT_GT(peakL, 0.45f);
  EXPECT_LT(peakR, 1e-4f);
  pb = f.playback();
  EXPECT_EQ(pb.playing, 1);
  EXPECT_NEAR(pb.position_ms, 10.0 * kBlock / 48.0, 1.0);

  // Tạm dừng: sau vài block đầu ra im lặng và vị trí đứng yên.
  ASSERT_EQ(tc_pause(f.e), TC_OK);
  f.run(5);
  const double pausedAt = f.playback().position_ms;
  out = f.run(5);
  EXPECT_EQ(out[0], 0.0f);
  EXPECT_EQ(f.playback().position_ms, pausedAt);

  // Tua tới 450 ms rồi phát đến hết → sự kiện kết thúc.
  ASSERT_EQ(tc_seek(f.e, 450.0), TC_OK);
  EXPECT_NEAR(f.playback().position_ms, 450.0, 0.1);
  EXPECT_EQ(tc_seek(f.e, -1.0), TC_ERR_INVALID_ARG);
  ASSERT_EQ(tc_play(f.e), TC_OK);
  ASSERT_TRUE(f.waitFor(TC_EVENT_BEAT_ENDED, nullptr));
  pb = f.playback();
  EXPECT_EQ(pb.playing, 0);
  EXPECT_NEAR(pb.position_ms, 500.0, 0.1);

  // Phát lại sau khi hết bài → bắt đầu từ đầu.
  ASSERT_EQ(tc_play(f.e), TC_OK);
  f.run(2);
  EXPECT_LT(f.playback().position_ms, 20.0);
  std::remove(path.c_str());
}

TEST(EngineM1, LoadMissingFileReportsError) {
  Fixture f;
  ASSERT_EQ(tc_load_beat(f.e, "/khong/co/file.mp3"), TC_OK);  // lỗi báo qua sự kiện
  tc_event ev{};
  ASSERT_TRUE(f.waitFor(TC_EVENT_BEAT_LOADED, &ev));
  EXPECT_EQ(ev.code, TC_ERR_INVALID_ARG);
  EXPECT_EQ(f.playback().has_beat, 0);
  EXPECT_EQ(tc_load_beat(f.e, nullptr), TC_ERR_INVALID_ARG);
}

TEST(EngineM1, ReplacingBeatWhilePlayingIsSafe) {
  const std::string a = tonecore::test::tempPath("a.wav");
  const std::string b = tonecore::test::tempPath("b.wav");
  ASSERT_TRUE(
      tonecore::test::writeWav16(a, tonecore::test::sineInt16(220, 48000, 48000, 2), 48000, 2));
  ASSERT_TRUE(
      tonecore::test::writeWav16(b, tonecore::test::sineInt16(330, 48000, 24000, 2), 48000, 2));
  Fixture f;
  for (int round = 0; round < 3; ++round) {
    tc_load_beat(f.e, a.c_str());
    ASSERT_TRUE(f.waitFor(TC_EVENT_BEAT_LOADED, nullptr));
    tc_play(f.e);
    f.run(5);
    tc_load_beat(f.e, b.c_str());
    ASSERT_TRUE(f.waitFor(TC_EVENT_BEAT_LOADED, nullptr));
    EXPECT_NEAR(f.playback().duration_ms, 500.0, 0.1);
    EXPECT_EQ(f.playback().playing, 0);  // track mới bắt đầu ở trạng thái dừng
  }
  std::remove(a.c_str());
  std::remove(b.c_str());
}

TEST(EngineM1, PollEventsValidatesArguments) {
  Fixture f;
  EXPECT_EQ(tc_poll_events(nullptr, nullptr, 0), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_poll_events(f.e, nullptr, 4), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_poll_events(f.e, nullptr, 0), 0);
}

TEST(EngineM1, MetricsAcceptOlderStructSize) {
  Fixture f;
  // Binding v0.1: struct kết thúc ở cpu_load.
  struct MetricsV01 {
    uint32_t struct_size;
    int32_t state;
    uint32_t sample_rate;
    uint64_t frames_processed;
    uint64_t callbacks;
    uint32_t xruns;
    float cpu_load;
  };
  MetricsV01 old{};
  old.struct_size = sizeof(old);
  ASSERT_EQ(tc_get_metrics(f.e, reinterpret_cast<tc_metrics*>(&old)), TC_OK);
  EXPECT_EQ(old.state, TC_STATE_RUNNING);
  EXPECT_EQ(old.sample_rate, 48000u);
}

// Đo độ trễ qua toàn bộ engine với vòng loopback giả lập trễ 1234 mẫu (≈ 25,7 ms).
TEST(EngineM1, MeasuresLatencyThroughEngine) {
  Fixture f(kBlock);
  constexpr uint32_t kDelay = 1234;
  // Đang phát beat + bật nghe lại: phép đo phải tự tắt tiếng chúng.
  tc_set_param(f.e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f);
  ASSERT_EQ(tc_measure_latency_start(f.e), TC_OK);
  EXPECT_EQ(tc_measure_latency_start(f.e), TC_ERR_INVALID_STATE);  // đang đo

  std::vector<float> history;  // kênh trái đã phát
  std::vector<float> in(kBlock);
  std::vector<float> out(2 * kBlock);
  tc_event ev{};
  bool done = false;
  for (int b = 0; b < 2000 && !done; ++b) {
    const std::size_t base = history.size();
    for (uint32_t i = 0; i < kBlock; ++i) {
      const long long src = static_cast<long long>(base + i) - kDelay;
      in[i] = src >= 0 && static_cast<std::size_t>(src) < base
                  ? 0.1f * history[static_cast<std::size_t>(src)]
                  : 0.0f;
    }
    ASSERT_EQ(tc_engine_process_offline(f.e, in.data(), out.data(), kBlock), TC_OK);
    for (uint32_t i = 0; i < kBlock; ++i) history.push_back(out[2 * i]);
    for (const auto& e : f.poll()) {
      if (e.type == TC_EVENT_LATENCY_DONE) {
        ev = e;
        done = true;
      }
    }
  }
  // Phân tích chạy ở luồng nền: chờ thêm nếu cần.
  if (!done) done = f.waitFor(TC_EVENT_LATENCY_DONE, &ev);
  ASSERT_TRUE(done);
  EXPECT_EQ(ev.code, TC_OK);
  EXPECT_NEAR(ev.value, 1000.0 * kDelay / 48000.0, 0.05);

  tc_latency_result r{};
  r.struct_size = sizeof(r);
  ASSERT_EQ(tc_get_latency_result(f.e, &r), TC_OK);
  EXPECT_EQ(r.valid, 1);
  EXPECT_NEAR(r.latency_frames, kDelay, 1.0);
  EXPECT_EQ(r.valid_bursts, r.total_bursts);
  EXPECT_GT(r.confidence, 0.5f);

  // Đo xong thì đo lại được.
  EXPECT_EQ(tc_measure_latency_start(f.e), TC_OK);
}

TEST(EngineM1, LatencyRequiresRunningEngine) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  EXPECT_EQ(tc_measure_latency_start(e), TC_ERR_INVALID_STATE);
  tc_engine_destroy(e);
}
