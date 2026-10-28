// Test engine M4 qua C API: đổi tone beat (tc_set_transpose), key chỉnh giọng tự cộng nửa cung.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"
#include "util/stretch_eval.h"
#include "util/synth_voice.h"

namespace {

constexpr uint32_t kBlock = 256;
constexpr double kRate = 48000.0;

struct Fixture {
  tc_engine* e = nullptr;
  std::vector<float> voice;
  std::size_t vcur = 0;
  std::vector<float> out;  // stereo interleaved của lần run() gần nhất
  std::vector<tc_event> events;

  Fixture() {
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
  }
  ~Fixture() { tc_engine_destroy(e); }

  // Beat sin `hz` (biên độ 0,25), `seconds` giây; chờ nạp xong.
  void loadSine(double hz, double seconds) {
    loadPcm(
        tonecore::test::sineInt16(hz, 48000, static_cast<std::size_t>(kRate * seconds), 2, 0.25));
  }

  // Beat stereo 48 kHz từ PCM int16 interleaved; chờ nạp xong.
  void loadPcm(const std::vector<int16_t>& pcm) {
    const std::string path = tonecore::test::tempPath("m4_beat.wav");
    ASSERT_TRUE(tonecore::test::writeWav16(path, pcm, 48000, 2));
    events.clear();
    ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
    ASSERT_TRUE(waitFor([&] {
      poll();
      for (const auto& ev : events) {
        if (ev.type == TC_EVENT_BEAT_LOADED) return true;
      }
      return false;
    }));
  }

  void run(double seconds) {
    std::vector<float> in(kBlock, 0.0f);
    std::vector<float> o(2 * kBlock);
    out.clear();
    const auto blocks = static_cast<int>(std::ceil(seconds * kRate / kBlock));
    for (int b = 0; b < blocks; ++b) {
      for (uint32_t i = 0; i < kBlock && !voice.empty(); ++i) {
        in[i] = voice[vcur];
        vcur = (vcur + 1) % voice.size();
      }
      ASSERT_EQ(tc_engine_process_offline(e, in.data(), o.data(), kBlock), TC_OK);
      out.insert(out.end(), o.begin(), o.end());
    }
  }

  void poll() {
    tc_event buf[32];
    const int32_t n = tc_poll_events(e, buf, 32);
    events.insert(events.end(), buf, buf + (n > 0 ? n : 0));
  }

  tc_playback_state playback() {
    tc_playback_state s{};
    s.struct_size = sizeof(s);
    EXPECT_EQ(tc_get_playback_state(e, &s), TC_OK);
    return s;
  }

  // Chờ luồng nền render xong cả bài (không chạy audio: test xử lý nhanh hơn thời gian thực hàng
  // trăm lần, chạy song song thì đầu phát vượt luồng render).
  bool waitRendered() {
    return waitFor([&] { return playback().transpose_progress >= 1.0f; });
  }

  uint64_t positionFrames() {
    return static_cast<uint64_t>(std::llround(playback().position_ms * kRate / 1000.0));
  }

  // Chạy audio từng block (luồng nền render song song) tới khi beat nghe ở tone `semis`.
  bool runUntilAudible(int semis) {
    return waitFor([&] {
      run(kBlock / kRate);
      return playback().audible_transpose == semis;
    });
  }

  template <typename F>
  static bool waitFor(F cond, double timeoutSec = 30.0) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSec);
    while (std::chrono::steady_clock::now() < deadline) {
      if (cond()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }
};

// Tần số của kênh trái trong `out` bằng điểm cắt 0 (nội suy tuyến tính).
double frequencyOf(const std::vector<float>& stereo) {
  double first = -1.0;
  double last = -1.0;
  int crossings = 0;
  for (std::size_t i = 1; i < stereo.size() / 2; ++i) {
    const float a = stereo[2 * (i - 1)];
    const float b = stereo[2 * i];
    if (a < 0.0f && b >= 0.0f) {
      const double t = static_cast<double>(i - 1) + a / (a - b);
      if (first < 0.0) {
        first = t;
      } else {
        ++crossings;
      }
      last = t;
    }
  }
  return crossings > 0 ? kRate * crossings / (last - first) : 0.0;
}

}  // namespace

// Đổi +3 khi đang phát: nghe đúng 440·2^(3/12) Hz, không có bước nhảy (click) lúc chuyển; về 0 →
// lại 440 Hz.
TEST(EngineTranspose, ShiftsBeatPitchSmoothly) {
  Fixture f;
  f.loadSine(440.0, 6.0);
  ASSERT_EQ(tc_play(f.e), TC_OK);
  f.run(0.2);
  EXPECT_NEAR(frequencyOf(f.out), 440.0, 0.05);

  ASSERT_EQ(tc_set_transpose(f.e, 3), TC_OK);
  int32_t t = 0;
  ASSERT_EQ(tc_get_transpose(f.e, &t), TC_OK);
  EXPECT_EQ(t, 3);
  ASSERT_TRUE(f.waitRendered());
  std::vector<float> around;
  ASSERT_TRUE(f.waitFor([&] {
    f.run(kBlock / kRate);
    around.insert(around.end(), f.out.begin(), f.out.end());
    return f.playback().audible_transpose == 3;
  }));
  f.run(0.1);  // xong crossfade 20 ms
  around.insert(around.end(), f.out.begin(), f.out.end());
  float maxJump = 0.0f;
  for (std::size_t i = 1; i < around.size() / 2; ++i) {
    maxJump = std::max(maxJump, std::abs(around[2 * i] - around[2 * (i - 1)]));
  }
  // Sin 523 Hz biên độ 0,25: bước lớn nhất giữa hai mẫu ≈ 0,017.
  EXPECT_LT(maxJump, 0.03f);
  f.run(0.3);
  EXPECT_NEAR(frequencyOf(f.out), 440.0 * std::pow(2.0, 3.0 / 12.0), 0.1);

  ASSERT_EQ(tc_set_transpose(f.e, 0), TC_OK);
  ASSERT_TRUE(f.runUntilAudible(0));
  f.run(0.05);  // bỏ qua crossfade 20 ms
  f.run(0.3);
  EXPECT_NEAR(frequencyOf(f.out), 440.0, 0.05);
  EXPECT_EQ(tc_set_transpose(f.e, 7), TC_ERR_INVALID_ARG);
}

// Đổi tone giữa bài khi đang phát: beat đi tiếp ĐÚNG CHỖ của bài ở tone mới (không lặp / lệch
// đoạn), về Gốc thì trùng từng mẫu với beat gốc (lỗi nghe trên iPhone 2026-10-28: "lặp lại nhiều
// giây, về gốc cũng bị").
TEST(EngineTranspose, KeepsSongPositionAndReturnsToOriginalExactly) {
  Fixture f;
  const auto sig = tonecore::test::melodySignal(12.0, kRate);
  std::vector<int16_t> pcm(sig.stereo.size());
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<int16_t>(std::lround(sig.stereo[i] * 32767.0f));
  }
  f.loadPcm(pcm);
  // Đầu ra trễ đúng độ trễ thuật toán (limiter đầu ra 1 ms, M5) so với vị trí phát.
  tc_metrics m{};
  m.struct_size = sizeof(m);
  ASSERT_EQ(tc_get_metrics(f.e, &m), TC_OK);
  const auto lag = static_cast<std::size_t>(m.algorithm_latency_frames);
  const auto expectOriginal = [&](const char* when) {
    const uint64_t at = f.positionFrames();
    f.run(1.0);
    float worst = 0.0f;
    for (std::size_t i = 2 * lag; i < f.out.size(); ++i) {
      worst = std::max(
          worst, std::abs(f.out[i] - static_cast<float>(pcm[2 * at + i - 2 * lag]) / 32768.0f));
    }
    EXPECT_LT(worst, 1e-6f) << when;
  };
  ASSERT_EQ(tc_play(f.e), TC_OK);
  f.run(0.5);
  expectOriginal("trước khi đổi tone");

  for (const int semis : {3, -4}) {
    ASSERT_EQ(tc_set_transpose(f.e, semis), TC_OK);
    ASSERT_TRUE(f.waitRendered());
    ASSERT_TRUE(f.runUntilAudible(semis));
    f.run(0.05);  // xong crossfade 20 ms
    const uint64_t at = f.positionFrames();
    f.run(3.0);
    // Bỏ `lag` frame đầu (còn của trước limiter): mẫu ra i ứng với frame at + i − lag.
    const std::vector<float> aligned(f.out.begin() + static_cast<std::ptrdiff_t>(2 * lag),
                                     f.out.end());
    EXPECT_EQ(tonecore::test::misplacedNotes(sig, aligned, semis, at), std::vector<int>{}) << semis;
  }

  ASSERT_EQ(tc_set_transpose(f.e, 0), TC_OK);
  ASSERT_TRUE(f.runUntilAudible(0));
  f.run(0.05);
  expectOriginal("về Gốc");
}

// Key chỉnh giọng: tc_get_key giữ tone beat gốc, tc_get_applied_key cộng số nửa cung đang nghe,
// có TC_EVENT_KEY_CHANGED; người hát C#4 trong Đô trưởng bị kéo về C/D, sau khi beat lên +2 (Rê
// trưởng) thì C# là nốt của thang → nốt đích 61.
TEST(EngineTranspose, CorrectionKeyFollowsAudibleTone) {
  Fixture f;
  f.loadSine(261.63, 6.0);
  ASSERT_EQ(tc_set_key(f.e, 0, TC_SCALE_MAJOR), TC_OK);
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  f.voice = tonecore::test::synthVoice([](double) { return 277.18; }, 4.0).audio;
  ASSERT_EQ(tc_play(f.e), TC_OK);
  f.run(1.0);
  tc_pitch_state ps{};
  ps.struct_size = sizeof(ps);
  ASSERT_EQ(tc_get_pitch_state(f.e, &ps), TC_OK);
  EXPECT_TRUE(ps.target_note == 60 || ps.target_note == 62) << ps.target_note;

  f.events.clear();
  ASSERT_EQ(tc_set_transpose(f.e, 2), TC_OK);
  ASSERT_TRUE(f.runUntilAudible(2));
  f.run(1.0);
  ASSERT_EQ(tc_get_pitch_state(f.e, &ps), TC_OK);
  EXPECT_EQ(ps.target_note, 61);
  int32_t root = -1;
  int32_t scale = -1;
  ASSERT_EQ(tc_get_key(f.e, &root, &scale), TC_OK);
  EXPECT_EQ(root, 0);
  ASSERT_EQ(tc_get_applied_key(f.e, &root, &scale), TC_OK);
  EXPECT_EQ(root, 2);
  EXPECT_EQ(scale, TC_SCALE_MAJOR);
  f.poll();
  bool keyEvent = false;
  for (const auto& ev : f.events) {
    if (ev.type == TC_EVENT_KEY_CHANGED && ev.code == 2) keyEvent = true;
  }
  EXPECT_TRUE(keyEvent);
}

// Timeline (tone beat gốc) + đổi tone: key áp dụng = đoạn timeline + số nửa cung.
TEST(EngineTranspose, TimelinePlusTranspose) {
  Fixture f;
  f.loadSine(220.0, 4.0);
  const tc_key_segment seg{0.0, 4000.0, 9, TC_SCALE_NATURAL_MINOR, 0.9f};
  ASSERT_EQ(tc_set_key_timeline(f.e, &seg, 1), TC_OK);
  ASSERT_EQ(tc_play(f.e), TC_OK);
  f.run(0.1);
  ASSERT_EQ(tc_set_transpose(f.e, -2), TC_OK);
  ASSERT_TRUE(f.runUntilAudible(-2));
  int32_t root = -1;
  int32_t scale = -1;
  ASSERT_EQ(tc_get_key(f.e, &root, &scale), TC_OK);
  EXPECT_EQ(root, 9);
  ASSERT_EQ(tc_get_applied_key(f.e, &root, &scale), TC_OK);
  EXPECT_EQ(root, 7);
  EXPECT_EQ(scale, TC_SCALE_NATURAL_MINOR);
}

// Render xong cả bài → TC_EVENT_TRANSPOSE_READY, tiến độ 1; nạp beat mới → tone về 0.
TEST(EngineTranspose, ReadyEventAndResetOnLoad) {
  Fixture f;
  f.loadSine(440.0, 3.0);
  EXPECT_EQ(tc_set_transpose(f.e, -4), TC_OK);
  bool ready = false;
  ASSERT_TRUE(f.waitFor([&] {
    f.poll();
    for (const auto& ev : f.events) {
      if (ev.type == TC_EVENT_TRANSPOSE_READY && ev.code == -4) ready = true;
    }
    return ready;
  }));
  EXPECT_FLOAT_EQ(f.playback().transpose_progress, 1.0f);
  EXPECT_EQ(f.playback().transpose, -4);
  f.loadSine(330.0, 2.0);
  EXPECT_EQ(f.playback().transpose, 0);
  f.run(0.05);
  EXPECT_EQ(f.playback().audible_transpose, 0);
}

// Chưa nạp beat → không đổi tone được.
TEST(EngineTranspose, RequiresBeat) {
  Fixture f;
  EXPECT_EQ(tc_set_transpose(f.e, 2), TC_ERR_INVALID_STATE);
}

// Binding v0.4 (struct kết thúc ở duration_ms) vẫn đọc được trạng thái phát.
TEST(EngineTranspose, PlaybackStateAcceptsV04Struct) {
  Fixture f;
  tc_playback_state s{};
  s.struct_size = static_cast<uint32_t>(offsetof(tc_playback_state, duration_ms) + sizeof(double));
  s.transpose = 99;
  EXPECT_EQ(tc_get_playback_state(f.e, &s), TC_OK);
  EXPECT_EQ(s.transpose, 99);  // không bị ghi đè ngoài phần người gọi có
  s.struct_size = 8;
  EXPECT_EQ(tc_get_playback_state(f.e, &s), TC_ERR_INVALID_ARG);
}
