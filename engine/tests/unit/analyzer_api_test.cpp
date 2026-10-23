// Kiểm tra C API phân tích bài (tc_analyzer_*, v0.4) đúng như Dart FFI sẽ gọi.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"
#include "util/synth_music.h"

namespace {

struct AnalyzerHolder {
  tc_analyzer* a = nullptr;
  AnalyzerHolder() { EXPECT_EQ(tc_analyzer_create(&a), TC_OK); }
  ~AnalyzerHolder() { tc_analyzer_destroy(a); }
};

tc_analysis_status status(tc_analyzer* a) {
  tc_analysis_status s{};
  s.struct_size = sizeof(s);
  EXPECT_EQ(tc_analyzer_get_status(a, &s), TC_OK);
  return s;
}

// Kéo trạng thái như UI (mỗi 5 ms) tới khi không còn RUNNING; tối đa 120 s (bản Debug / TSan).
tc_analysis_status waitFinished(tc_analyzer* a) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  tc_analysis_status s = status(a);
  float lastProgress = 0.0f;
  while (s.state == TC_ANALYSIS_RUNNING && std::chrono::steady_clock::now() < deadline) {
    EXPECT_GE(s.progress, lastProgress);  // tiến độ chỉ tăng
    lastProgress = s.progress;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    s = status(a);
  }
  return s;
}

// Vòng hoà âm Rê trưởng 16 s + tiếng gõ mỗi 0,5 s (120 BPM), stereo 44,1 kHz.
std::string writeCadenceWav(const std::string& name) {
  const double rate = 44100.0;
  std::vector<float> x(static_cast<std::size_t>(rate * 16.0), 0.0f);
  tonecore::test::NoteSpec spec;
  spec.harmonics = 4;
  tonecore::test::addCadence(x, rate, 0.0, 2, false, 2, 2.0, 7, spec);
  for (int i = 0; i < 32; ++i) tonecore::test::addClick(x, rate, 0.5 * i, 0.4f, 40 + i);
  std::vector<int16_t> pcm(x.size() * 2);
  for (std::size_t i = 0; i < x.size(); ++i) {
    const float v = std::fmax(-1.0f, std::fmin(1.0f, x[i]));
    pcm[2 * i] = pcm[2 * i + 1] = static_cast<int16_t>(std::lround(v * 32767.0f));
  }
  const std::string path = tonecore::test::tempPath(name);
  EXPECT_TRUE(tonecore::test::writeWav16(path, pcm, 44100, 2));
  return path;
}

std::string writeLongSineWav(const std::string& name, double seconds) {
  const std::string path = tonecore::test::tempPath(name);
  EXPECT_TRUE(tonecore::test::writeWav16(
      path, tonecore::test::sineInt16(440.0, 44100, static_cast<std::size_t>(44100 * seconds), 2),
      44100, 2));
  return path;
}

}  // namespace

TEST(AnalyzerApi, LifecycleAndArguments) {
  EXPECT_EQ(tc_analyzer_create(nullptr), TC_ERR_INVALID_ARG);
  tc_analyzer_destroy(nullptr);  // cho phép
  AnalyzerHolder h;
  ASSERT_NE(h.a, nullptr);
  const tc_analysis_status s = status(h.a);
  EXPECT_EQ(s.state, TC_ANALYSIS_IDLE);
  EXPECT_EQ(s.stage, TC_STAGE_NONE);
  EXPECT_EQ(s.progress, 0.0f);

  tc_analysis_result r{};
  r.struct_size = sizeof(r);
  EXPECT_EQ(tc_analyzer_get_result(h.a, &r), TC_ERR_INVALID_STATE);
  EXPECT_EQ(tc_analyzer_get_key_timeline(h.a, nullptr, 0), TC_ERR_INVALID_STATE);
  EXPECT_EQ(tc_analyzer_get_key_timeline(h.a, nullptr, 3), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_analyzer_start(h.a, nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_analyzer_start(h.a, ""), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_analyzer_start(nullptr, "x.mp3"), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_analyzer_cancel(nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_analyzer_cancel(h.a), TC_OK);  // chưa chạy: không làm gì

  tc_analysis_status bad{};
  bad.struct_size = 4;
  EXPECT_EQ(tc_analyzer_get_status(h.a, &bad), TC_ERR_INVALID_ARG);
  EXPECT_STREQ(tc_result_to_string(TC_ERR_CANCELLED), "TC_ERR_CANCELLED");
}

TEST(AnalyzerApi, AnalyzesFile) {
  const std::string path = writeCadenceWav("analyzer_cadence.wav");
  AnalyzerHolder h;
  ASSERT_EQ(tc_analyzer_start(h.a, path.c_str()), TC_OK);
  const tc_analysis_status s = waitFinished(h.a);
  ASSERT_EQ(s.state, TC_ANALYSIS_DONE) << tc_result_to_string(s.result);
  EXPECT_EQ(s.result, TC_OK);
  EXPECT_EQ(s.stage, TC_STAGE_DONE);
  EXPECT_FLOAT_EQ(s.progress, 1.0f);

  tc_analysis_result r{};
  r.struct_size = sizeof(r);
  ASSERT_EQ(tc_analyzer_get_result(h.a, &r), TC_OK);
  EXPECT_NEAR(r.duration_ms, 16000.0, 1.0);
  EXPECT_EQ(r.sample_rate, 44100u);
  EXPECT_EQ(r.channels, 2u);
  ASSERT_EQ(r.has_key, 1);
  EXPECT_EQ(r.key_root, 2);
  EXPECT_EQ(r.key_scale, TC_SCALE_MAJOR);
  EXPECT_GT(r.key_confidence, 0.0f);
  ASSERT_EQ(r.has_bpm, 1);
  EXPECT_NEAR(r.bpm, 120.0f, 1.2f);
  ASSERT_EQ(r.has_loudness, 1);
  EXPECT_LT(r.integrated_lufs, 0.0f);
  ASSERT_EQ(r.has_tuning, 1);
  EXPECT_NEAR(r.tuning_cents, 0.0f, 3.0f);
  ASSERT_GE(r.timeline_count, 1u);

  EXPECT_EQ(tc_analyzer_get_key_timeline(h.a, nullptr, 0), static_cast<int32_t>(r.timeline_count));
  std::vector<tc_key_segment> segs(r.timeline_count + 2);
  const int32_t n =
      tc_analyzer_get_key_timeline(h.a, segs.data(), static_cast<uint32_t>(segs.size()));
  ASSERT_EQ(n, static_cast<int32_t>(r.timeline_count));
  EXPECT_DOUBLE_EQ(segs[0].start_ms, 0.0);
  EXPECT_NEAR(segs[static_cast<std::size_t>(n - 1)].end_ms, r.duration_ms, 1e-6);
  EXPECT_EQ(segs[0].root, 2);
  EXPECT_EQ(segs[0].scale, TC_SCALE_MAJOR);
  // Mảng nhỏ hơn số đoạn → ghi đúng max đoạn.
  EXPECT_EQ(tc_analyzer_get_key_timeline(h.a, segs.data(), 1), 1);

  tc_analysis_result small{};
  small.struct_size = 8;
  EXPECT_EQ(tc_analyzer_get_result(h.a, &small), TC_ERR_INVALID_ARG);
  std::remove(path.c_str());
}

TEST(AnalyzerApi, MissingFileFails) {
  AnalyzerHolder h;
  ASSERT_EQ(tc_analyzer_start(h.a, "/khong/ton/tai.mp3"), TC_OK);  // lỗi báo qua trạng thái
  const tc_analysis_status s = waitFinished(h.a);
  EXPECT_EQ(s.state, TC_ANALYSIS_FAILED);
  EXPECT_EQ(s.result, TC_ERR_INVALID_ARG);
  tc_analysis_result r{};
  r.struct_size = sizeof(r);
  EXPECT_EQ(tc_analyzer_get_result(h.a, &r), TC_ERR_INVALID_STATE);
}

TEST(AnalyzerApi, CancelAndRestart) {
  const std::string path = writeLongSineWav("analyzer_long.wav", 120.0);
  AnalyzerHolder h;
  ASSERT_EQ(tc_analyzer_start(h.a, path.c_str()), TC_OK);
  EXPECT_EQ(tc_analyzer_start(h.a, path.c_str()), TC_ERR_INVALID_STATE);  // đang chạy
  ASSERT_EQ(tc_analyzer_cancel(h.a), TC_OK);
  tc_analysis_status s = waitFinished(h.a);
  // Máy cực nhanh có thể xong trước khi kịp huỷ; cả hai đều hợp lệ.
  if (s.state == TC_ANALYSIS_CANCELLED) {
    EXPECT_EQ(s.result, TC_ERR_CANCELLED);
  } else {
    EXPECT_EQ(s.state, TC_ANALYSIS_DONE);
  }
  // Dùng lại analyzer sau khi huỷ: cờ huỷ cũ không ảnh hưởng lần chạy mới.
  ASSERT_EQ(tc_analyzer_start(h.a, path.c_str()), TC_OK);
  s = waitFinished(h.a);
  EXPECT_EQ(s.state, TC_ANALYSIS_DONE) << tc_result_to_string(s.result);
  std::remove(path.c_str());
}

// Huỷ đối tượng khi đang phân tích: phải dừng nhanh, không treo, không lỗi bộ nhớ (ASan/TSan).
TEST(AnalyzerApi, DestroyWhileRunning) {
  const std::string path = writeLongSineWav("analyzer_destroy.wav", 120.0);
  for (int i = 0; i < 3; ++i) {
    tc_analyzer* a = nullptr;
    ASSERT_EQ(tc_analyzer_create(&a), TC_OK);
    ASSERT_EQ(tc_analyzer_start(a, path.c_str()), TC_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(20 * i));
    const auto t0 = std::chrono::steady_clock::now();
    tc_analyzer_destroy(a);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(10));
  }
  std::remove(path.c_str());
}
