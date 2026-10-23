// Test engine M3 qua C API: tuning của lưới nốt, tự đổi key theo timeline của bài.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"
#include "util/synth_voice.h"

namespace {

constexpr uint32_t kBlock = 256;
constexpr double kRate = 48000.0;

struct Fixture {
  tc_engine* e = nullptr;
  std::vector<float> voice;  // micro (giọng tổng hợp), lặp vòng
  std::size_t cursor = 0;

  Fixture() {
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
  }
  ~Fixture() { tc_engine_destroy(e); }

  // Giọng giữ một cao độ cố định (Hz).
  void singConstant(double hz) {
    voice = tonecore::test::synthVoice([hz](double) { return hz; }, 4.0).audio;
    cursor = 0;
  }

  void run(double seconds) {
    std::vector<float> in(kBlock, 0.0f);
    std::vector<float> out(2 * kBlock);
    const auto blocks = static_cast<int>(std::ceil(seconds * kRate / kBlock));
    for (int b = 0; b < blocks; ++b) {
      for (uint32_t i = 0; i < kBlock && !voice.empty(); ++i) {
        in[i] = voice[cursor];
        cursor = (cursor + 1) % voice.size();
      }
      ASSERT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
    }
  }

  tc_pitch_state pitch() {
    tc_pitch_state s{};
    s.struct_size = sizeof(s);
    EXPECT_EQ(tc_get_pitch_state(e, &s), TC_OK);
    return s;
  }

  std::pair<int32_t, int32_t> key() {
    int32_t root = -1;
    int32_t scale = -1;
    EXPECT_EQ(tc_get_key(e, &root, &scale), TC_OK);
    return {root, scale};
  }

  std::vector<tc_event> poll() {
    tc_event buf[32];
    const int32_t n = tc_poll_events(e, buf, 32);
    EXPECT_GE(n, 0);
    return std::vector<tc_event>(buf, buf + (n > 0 ? n : 0));
  }

  // Nạp beat im lặng `seconds` giây rồi phát.
  void loadSilentBeat(double seconds) {
    const std::string path = tonecore::test::tempPath("m3_beat.wav");
    std::vector<int16_t> pcm(static_cast<std::size_t>(kRate * seconds) * 2, 0);
    ASSERT_TRUE(tonecore::test::writeWav16(path, pcm, 48000, 2));
    ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool loaded = false;
    while (!loaded && std::chrono::steady_clock::now() < deadline) {
      for (const tc_event& ev : poll()) {
        if (ev.type == TC_EVENT_BEAT_LOADED) {
          ASSERT_EQ(ev.code, TC_OK);
          loaded = true;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(loaded);
    std::remove(path.c_str());
    ASSERT_EQ(tc_play(e), TC_OK);
  }
};

tc_key_segment seg(double startMs, double endMs, int32_t root, int32_t scale) {
  tc_key_segment s{};
  s.start_ms = startMs;
  s.end_ms = endMs;
  s.root = root;
  s.scale = scale;
  return s;
}

bool hasKeyChanged(const std::vector<tc_event>& evs, int32_t root, int32_t scale) {
  for (const tc_event& ev : evs) {
    if (ev.type == TC_EVENT_KEY_CHANGED && ev.code == root && ev.value == scale) return true;
  }
  return false;
}

}  // namespace

TEST(EngineM3, TuningValidation) {
  Fixture f;
  float cents = 1.0f;
  ASSERT_EQ(tc_get_tuning(f.e, &cents), TC_OK);
  EXPECT_EQ(cents, 0.0f);
  EXPECT_EQ(tc_set_tuning(f.e, std::numeric_limits<float>::quiet_NaN()), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_tuning(f.e, 150.0f), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_tuning(nullptr, 0.0f), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_get_tuning(f.e, nullptr), TC_ERR_INVALID_ARG);
  ASSERT_EQ(tc_set_tuning(f.e, 18.5f), TC_OK);
  ASSERT_EQ(tc_get_tuning(f.e, &cents), TC_OK);
  EXPECT_FLOAT_EQ(cents, 18.5f);
}

// Giọng hát đúng La4 + 30 cent. Lưới 440 Hz → engine kéo xuống 30 cent; beat cũng lệch +30 cent
// (tc_set_tuning) → giọng đã đúng cao độ của beat, không chỉnh gì.
TEST(EngineM3, TuningShiftsCorrectionGrid) {
  for (const float tuning : {0.0f, 30.0f}) {
    SCOPED_TRACE(tuning);
    Fixture f;
    f.singConstant(440.0 * std::pow(2.0, 30.0 / 1200.0));
    ASSERT_EQ(tc_set_key(f.e, 0, TC_SCALE_CHROMATIC), TC_OK);
    ASSERT_EQ(tc_set_correction_preset(f.e, TC_PRESET_ROBOT), TC_OK);
    ASSERT_EQ(tc_set_param(f.e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
    ASSERT_EQ(tc_set_tuning(f.e, tuning), TC_OK);
    f.run(1.0);
    const tc_pitch_state s = f.pitch();
    ASSERT_EQ(s.voiced, 1);
    EXPECT_EQ(s.target_note, 69);
    EXPECT_NEAR(s.input_note, 69.3f, 0.05f);  // đồng hồ vẫn báo cao độ tuyệt đối
    EXPECT_NEAR(s.correction_cents, tuning - 30.0f, 4.0f);
    EXPECT_NEAR(s.cents_off, 30.0f - tuning, 4.0f);
  }
}

// Key theo vị trí beat: C trưởng 0–1 s, La thứ 1–2 s, Rê trưởng 2–3 s; tua về thì key theo vị trí;
// chọn key tay → tắt chế độ theo timeline.
TEST(EngineM3, KeyTimelineFollowsBeatPosition) {
  Fixture f;
  f.loadSilentBeat(3.0);
  const tc_key_segment tl[] = {seg(0, 1000, 0, TC_SCALE_MAJOR),
                               seg(1000, 2000, 9, TC_SCALE_NATURAL_MINOR),
                               seg(2000, 3000, 2, TC_SCALE_MAJOR)};
  ASSERT_EQ(tc_set_key_timeline(f.e, tl, 3), TC_OK);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{0, TC_SCALE_MAJOR}));  // đúng ngay, chưa chạy

  f.run(0.5);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{0, TC_SCALE_MAJOR}));
  EXPECT_TRUE(hasKeyChanged(f.poll(), 0, TC_SCALE_MAJOR));
  f.run(0.7);  // qua mốc 1 s
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{9, TC_SCALE_NATURAL_MINOR}));
  EXPECT_TRUE(hasKeyChanged(f.poll(), 9, TC_SCALE_NATURAL_MINOR));

  ASSERT_EQ(tc_seek(f.e, 2500.0), TC_OK);
  f.run(0.05);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{2, TC_SCALE_MAJOR}));
  ASSERT_EQ(tc_seek(f.e, 100.0), TC_OK);
  f.run(0.05);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{0, TC_SCALE_MAJOR}));
  f.poll();

  // Chọn tay Fa trưởng: giữ nguyên dù beat chạy qua mốc 1 s, không còn sự kiện tự đổi key.
  ASSERT_EQ(tc_set_key(f.e, 5, TC_SCALE_MAJOR), TC_OK);
  f.run(1.2);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{5, TC_SCALE_MAJOR}));
  for (const tc_event& ev : f.poll()) EXPECT_NE(ev.type, TC_EVENT_KEY_CHANGED);

  // Đặt lại timeline → theo timeline tiếp (vị trí ~1,3 s → La thứ).
  ASSERT_EQ(tc_set_key_timeline(f.e, tl, 3), TC_OK);
  f.run(0.05);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{9, TC_SCALE_NATURAL_MINOR}));
  // count = 0 → tắt, quay về key chọn tay gần nhất.
  ASSERT_EQ(tc_set_key_timeline(f.e, nullptr, 0), TC_OK);
  f.run(0.05);
  EXPECT_EQ(f.key(), (std::pair<int32_t, int32_t>{5, TC_SCALE_MAJOR}));
}

// Chỉnh giọng thực sự dùng key của timeline: hát Fa#4 thấp 30 cent (MIDI 65,7). Đoạn C trưởng
// (không có F#) → kéo về Fa (−70 cent); đoạn Sol trưởng (có F#) → kéo lên Fa# (+30 cent).
TEST(EngineM3, KeyTimelineDrivesCorrection) {
  Fixture f;
  f.singConstant(440.0 * std::pow(2.0, (65.7 - 69.0) / 12.0));
  ASSERT_EQ(tc_set_correction_preset(f.e, TC_PRESET_ROBOT), TC_OK);
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  f.loadSilentBeat(3.0);
  const tc_key_segment tl[] = {seg(0, 1500, 0, TC_SCALE_MAJOR), seg(1500, 3000, 7, TC_SCALE_MAJOR)};
  ASSERT_EQ(tc_set_key_timeline(f.e, tl, 2), TC_OK);
  f.run(1.0);
  tc_pitch_state s = f.pitch();
  ASSERT_EQ(s.voiced, 1);
  EXPECT_EQ(s.target_note, 65);
  EXPECT_NEAR(s.correction_cents, -70.0f, 5.0f);
  f.run(1.0);  // sang đoạn Sol trưởng
  s = f.pitch();
  EXPECT_EQ(s.target_note, 66);
  EXPECT_NEAR(s.correction_cents, 30.0f, 5.0f);
}

TEST(EngineM3, KeyTimelineValidation) {
  Fixture f;
  const tc_key_segment ok = seg(0, 1000, 0, TC_SCALE_MAJOR);
  EXPECT_EQ(tc_set_key_timeline(nullptr, &ok, 1), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_key_timeline(f.e, nullptr, 1), TC_ERR_INVALID_ARG);
  const tc_key_segment bad[][2] = {
      {seg(0, 1000, 12, TC_SCALE_MAJOR), ok},                                // chủ âm sai
      {seg(0, 1000, 0, 9), ok},                                              // thang sai
      {seg(0, 1000, 0, TC_SCALE_MAJOR), seg(500, 1500, 2, TC_SCALE_MAJOR)},  // chồng nhau
      {seg(1000, 2000, 0, TC_SCALE_MAJOR), seg(0, 500, 2, TC_SCALE_MAJOR)},  // không tăng dần
      {seg(-5, 1000, 0, TC_SCALE_MAJOR), ok},                                // âm
      {seg(100, 100, 0, TC_SCALE_MAJOR), ok},                                // rỗng
  };
  for (const auto& b : bad) EXPECT_EQ(tc_set_key_timeline(f.e, b, 2), TC_ERR_INVALID_ARG);
  std::vector<tc_key_segment> many(257);
  for (std::size_t i = 0; i < many.size(); ++i) {
    many[i] = seg(static_cast<double>(i) * 10, static_cast<double>(i) * 10 + 10, 0, TC_SCALE_MAJOR);
  }
  EXPECT_EQ(tc_set_key_timeline(f.e, many.data(), 257), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_key_timeline(f.e, many.data(), 256), TC_OK);
  // Có khoảng trống giữa hai đoạn là hợp lệ (giữ key đoạn trước).
  const tc_key_segment gap[] = {seg(0, 1000, 0, TC_SCALE_MAJOR),
                                seg(1500, 2000, 2, TC_SCALE_MAJOR)};
  EXPECT_EQ(tc_set_key_timeline(f.e, gap, 2), TC_OK);
}
