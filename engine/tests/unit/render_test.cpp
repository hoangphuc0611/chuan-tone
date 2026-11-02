// Render lại bản thu (M7 B3b): đỉnh thật, limiter đỉnh thật, đo độ to theo khối, tc_renderer_*.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "decode/decoder.h"
#include "fx/true_peak.h"
#include "key/loudness.h"
#include "record/wav_file.h"
#include "test_util.h"
#include "tonecore/tonecore.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> stereoSine(double freq, double amp, double phase, std::size_t frames) {
  std::vector<float> v(2 * frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const auto s = static_cast<float>(
        amp * std::sin(2.0 * kPi * freq * static_cast<double>(i) / kRate + phase));
    v[2 * i] = s;
    v[2 * i + 1] = s;
  }
  return v;
}

float samplePeak(const std::vector<float>& x) {
  float p = 0.0f;
  for (const float v : x) p = std::max(p, std::fabs(v));
  return p;
}

// Thu một bản thu bằng engine backend NULL: beat (nếu có) + micro theo từng block.
struct TakeFixture {
  std::string dir;
  uint64_t frames = 0;

  TakeFixture(const std::string& name, const std::vector<float>& mic, bool withBeat) {
    dir = tonecore::test::tempPath(name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    tc_engine* e = nullptr;
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
    if (withBeat) {
      const std::string beat = dir + "/src_beat.wav";
      EXPECT_TRUE(tonecore::test::writeWav16(
          beat, tonecore::test::sineInt16(440.0, kRate, kRate * 4, 2, 0.25), kRate, 2));
      EXPECT_EQ(tc_load_beat(e, beat.c_str()), TC_OK);
      bool loaded = false;
      for (int i = 0; i < 5000 && !loaded; ++i) {
        tc_event ev[8];
        const int32_t n = tc_poll_events(e, ev, 8);
        for (int32_t k = 0; k < n; ++k) loaded = loaded || ev[k].type == TC_EVENT_BEAT_LOADED;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      EXPECT_TRUE(loaded);
      EXPECT_EQ(tc_play(e), TC_OK);
    }
    EXPECT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
    std::vector<float> out(2 * kBlock);
    for (std::size_t at = 0; at + kBlock <= mic.size(); at += kBlock) {
      EXPECT_EQ(tc_engine_process_offline(e, mic.data() + at, out.data(), kBlock), TC_OK);
    }
    EXPECT_EQ(tc_record_stop(e), TC_OK);
    tc_record_status s{};
    s.struct_size = sizeof(s);
    tc_record_get_status(e, &s);
    frames = s.frames;
    tc_engine_destroy(e);
  }
  ~TakeFixture() { fs::remove_all(dir); }
  std::string beat() const { return dir + "/beat.wav"; }
  std::string dry() const { return dir + "/dry.wav"; }
};

tc_render_status render(const std::string& beat, const std::string& dry, const std::string& out,
                        const tc_render_config& cfg) {
  tc_renderer* r = nullptr;
  EXPECT_EQ(tc_renderer_create(&r), TC_OK);
  EXPECT_EQ(tc_renderer_start(r, beat.c_str(), dry.c_str(), out.c_str(), &cfg), TC_OK);
  tc_render_status s{};
  s.struct_size = sizeof(s);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(tc_renderer_get_status(r, &s), TC_OK);
  } while (s.state == TC_ANALYSIS_RUNNING && std::chrono::steady_clock::now() < deadline);
  tc_renderer_destroy(r);
  return s;
}

}  // namespace

TEST(TruePeak, FindsInterSamplePeaks) {
  // sin fs/4 lệch pha 45°: mẫu chỉ chạm ±0,707, đỉnh thật = 1.
  const auto x = stereoSine(12000.0, 1.0, kPi / 4.0, kRate / 2);
  EXPECT_NEAR(samplePeak(x), 0.7071, 1e-3);
  EXPECT_NEAR(tonecore::measureTruePeak(x.data(), x.size() / 2, 2), 1.0, 0.02);
  // Tần số thấp: đỉnh thật ≈ đỉnh mẫu.
  const auto low = stereoSine(997.0, 0.5, 0.3, kRate / 2);
  EXPECT_NEAR(tonecore::measureTruePeak(low.data(), low.size() / 2, 2), 0.5, 0.005);
}

TEST(TruePeak, LimiterKeepsTruePeakUnderCeiling) {
  const float ceiling = std::pow(10.0f, -1.0f / 20.0f);  // −1 dBTP
  // Mẫu 0,95 nhưng đỉnh thật 1,34 (+2,6 dBTP).
  auto x = stereoSine(12000.0, 0.95 / 0.7071, kPi / 4.0, kRate);
  tonecore::TruePeakLimiter lim(2, kRate, ceiling);
  const auto L = static_cast<std::size_t>(lim.latency());
  x.resize(x.size() + 2 * L, 0.0f);
  std::vector<float> out(x.size());
  lim.process(x.data(), out.data(), static_cast<uint32_t>(x.size() / 2));
  const float tp = tonecore::measureTruePeak(out.data() + 2 * L, out.size() / 2 - L, 2);
  EXPECT_LE(tp, ceiling * 1.005f);
  EXPECT_GT(tp, ceiling * 0.9f);  // không hạ quá tay

  // Dưới trần: đi thẳng, chỉ trễ.
  const auto quiet = stereoSine(440.0, 0.3, 0.0, kRate / 4);
  tonecore::TruePeakLimiter lim2(2, kRate, ceiling);
  std::vector<float> in2(quiet);
  in2.resize(quiet.size() + 2 * L, 0.0f);
  std::vector<float> out2(in2.size());
  lim2.process(in2.data(), out2.data(), static_cast<uint32_t>(in2.size() / 2));
  for (std::size_t i = 0; i < quiet.size(); ++i) ASSERT_FLOAT_EQ(out2[i + 2 * L], quiet[i]);
}

TEST(Loudness, BlockMeterMatchesWholeFile) {
  tonecore::DecodedAudio a;
  a.sampleRate = kRate;
  a.channels = 2;
  a.samples = tonecore::test::sineInt16(1000.0, kRate, kRate * 5, 2, 0.1);
  a.frames = kRate * 5;
  const auto whole = tonecore::measureLoudness(a);
  tonecore::LoudnessMeter m(2, kRate);
  std::vector<float> f(a.samples.size());
  for (std::size_t i = 0; i < f.size(); ++i) f[i] = a.samples[i] / 32768.0f;
  for (std::size_t at = 0; at < a.frames; at += 1000) {
    m.process(f.data() + 2 * at, std::min<std::size_t>(1000, a.frames - at));
  }
  ASSERT_TRUE(whole.integratedLufs && m.result().integratedLufs);
  EXPECT_NEAR(*m.result().integratedLufs, *whole.integratedLufs, 1e-3);
  EXPECT_NEAR(*whole.integratedLufs, -20.0, 0.1);  // sin 1 kHz −20 dBFS stereo (EBU 3341)
}

TEST(RenderApi, NormalizesToTargetUnderTruePeak) {
  std::vector<float> mic(2 * kRate);
  for (std::size_t i = 0; i < mic.size(); ++i) {
    mic[i] = static_cast<float>(0.2 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / kRate));
  }
  TakeFixture take("render_norm", mic, true);
  const std::string out = take.dir + "/mix.wav";
  tc_render_config cfg;
  tc_render_config_init(&cfg);
  cfg.voice_offset_frames = 600;
  const tc_render_status s = render(take.beat(), take.dry(), out, cfg);
  ASSERT_EQ(s.state, TC_ANALYSIS_DONE) << s.result;
  EXPECT_EQ(s.frames, take.frames);
  EXPECT_EQ(s.sample_rate, kRate);
  EXPECT_NEAR(s.output_lufs, -14.0, 0.5);
  EXPECT_LE(s.true_peak_db, -1.0f + 0.05f);
  EXPECT_FALSE(std::isnan(s.mix_lufs));
  EXPECT_NEAR(s.gain_db, -14.0f - s.mix_lufs, 1e-3);
  EXPECT_FALSE(fs::exists(out + ".mix.tmp"));
  // Đo lại file bằng bộ giải mã: đúng độ dài, đúng độ to.
  tonecore::DecodedAudio a;
  ASSERT_EQ(tonecore::decodeFile(out, a), TC_OK);
  EXPECT_EQ(a.frames, take.frames);
  EXPECT_NEAR(*tonecore::measureLoudness(a).integratedLufs, -14.0, 0.5);

  // Cắt 0,5–1,5 s, không chuẩn hoá: đúng độ dài, fade ở hai đầu.
  cfg.start_frame = kRate / 2;
  cfg.end_frame = kRate * 3 / 2;
  cfg.normalize = 0;
  cfg.format = TC_RENDER_WAV_FLOAT;
  const tc_render_status t = render(take.beat(), take.dry(), out, cfg);
  ASSERT_EQ(t.state, TC_ANALYSIS_DONE);
  EXPECT_EQ(t.frames, kRate);
  EXPECT_FLOAT_EQ(t.gain_db, 0.0f);
  tonecore::WavData w;
  ASSERT_EQ(tonecore::readWav(out, w), TC_OK);
  ASSERT_EQ(w.frames, kRate);
  EXPECT_EQ(w.samples[0], 0.0f);
  EXPECT_LT(std::fabs(w.samples[2 * (kRate - 1)]), 1e-6);
  EXPECT_GT(samplePeak(w.samples), 0.3f);
}

// Giọng ra đúng vị trí: xung trong giọng khô dịch sớm voice_offset_frames, không còn trễ limiter.
TEST(RenderApi, VoiceIsAlignedLikeReview) {
  std::vector<float> mic(kRate, 0.0f);
  const int clicks[] = {10000, 20000, 30000};
  for (const int c : clicks) mic[c] = 0.5f;
  TakeFixture take("render_align", mic, false);
  tc_render_config cfg;
  tc_render_config_init(&cfg);
  cfg.voice_offset_frames = 100;
  cfg.normalize = 0;
  cfg.beat_gain = 0.0f;
  cfg.fx_enabled = 0;
  cfg.format = TC_RENDER_WAV_FLOAT;
  const std::string out = take.dir + "/mix.wav";
  ASSERT_EQ(render(take.beat(), take.dry(), out, cfg).state, TC_ANALYSIS_DONE);
  tonecore::WavData w;
  ASSERT_EQ(tonecore::readWav(out, w), TC_OK);
  for (const int c : clicks) {
    const int want = c - 100;
    int best = want - 40;
    for (int i = want - 40; i < want + 40; ++i) {
      if (std::fabs(w.samples[2 * i]) > std::fabs(w.samples[2 * best])) best = i;
    }
    EXPECT_EQ(best, want);
    EXPECT_NEAR(w.samples[2 * best], 0.5, 0.05);
  }
}

TEST(RenderApi, ChainOptionsCancelAndErrors) {
  std::vector<float> mic(kRate);
  for (std::size_t i = 0; i < mic.size(); ++i) {
    mic[i] = static_cast<float>(0.2 * std::sin(2.0 * kPi * 233.0 * static_cast<double>(i) / kRate));
  }
  TakeFixture take("render_opts", mic, true);
  const std::string out = take.dir + "/mix.wav";
  tc_render_config cfg;
  tc_render_config_init(&cfg);
  tc_fx_preset_init(&cfg.fx, TC_GENRE_BOLERO);
  cfg.correction_enabled = 1;
  cfg.correction_preset = TC_PRESET_SINGER;
  cfg.gate_enabled = 1;
  const tc_key_segment segs[] = {{0.0, 500.0, 9, TC_SCALE_NATURAL_MINOR, 1.0f},
                                 {500.0, 1000.0, 0, TC_SCALE_MAJOR, 1.0f}};
  cfg.key_segments = segs;
  cfg.key_segment_count = 2;
  const tc_render_status s = render(take.beat(), take.dry(), out, cfg);
  EXPECT_EQ(s.state, TC_ANALYSIS_DONE) << s.result;

  // Sai tham số.
  tc_renderer* r = nullptr;
  ASSERT_EQ(tc_renderer_create(&r), TC_OK);
  tc_render_config bad = cfg;
  bad.struct_size = 8;
  EXPECT_EQ(tc_renderer_start(r, "a", "b", "c", &bad), TC_ERR_INVALID_ARG);
  bad = cfg;
  bad.start_frame = 100;
  bad.end_frame = 50;
  EXPECT_EQ(tc_renderer_start(r, "a", "b", "c", &bad), TC_ERR_INVALID_ARG);
  bad = cfg;
  bad.key_segments = nullptr;
  EXPECT_EQ(tc_renderer_start(r, "a", "b", "c", &bad), TC_ERR_INVALID_ARG);
  bad = cfg;
  bad.voice_gain = std::nanf("");
  EXPECT_EQ(tc_renderer_start(r, "a", "b", "c", &bad), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_renderer_start(r, nullptr, "b", "c", &cfg), TC_ERR_INVALID_ARG);

  // File không có → FAILED.
  ASSERT_EQ(tc_renderer_start(r, "/khong/co.wav", take.dry().c_str(), out.c_str(), &cfg), TC_OK);
  tc_render_status st{};
  st.struct_size = sizeof(st);
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_EQ(tc_renderer_get_status(r, &st), TC_OK);
  } while (st.state == TC_ANALYSIS_RUNNING);
  EXPECT_EQ(st.state, TC_ANALYSIS_FAILED);

  // Huỷ ngay sau khi bắt đầu: CANCELLED (hoặc DONE nếu đã kịp xong), không để file tạm.
  ASSERT_EQ(tc_renderer_start(r, take.beat().c_str(), take.dry().c_str(), out.c_str(), &cfg),
            TC_OK);
  EXPECT_EQ(tc_renderer_start(r, take.beat().c_str(), take.dry().c_str(), out.c_str(), &cfg),
            TC_ERR_INVALID_STATE);
  ASSERT_EQ(tc_renderer_cancel(r), TC_OK);
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_EQ(tc_renderer_get_status(r, &st), TC_OK);
  } while (st.state == TC_ANALYSIS_RUNNING);
  EXPECT_TRUE(st.state == TC_ANALYSIS_CANCELLED || st.state == TC_ANALYSIS_DONE);
  EXPECT_FALSE(fs::exists(out + ".mix.tmp"));
  EXPECT_FALSE(fs::exists(out + ".part"));
  tc_renderer_destroy(r);
  tc_renderer_destroy(nullptr);
}

// Đo tốc độ render bản thu 4 phút (Bolero + chỉnh giọng Ca sĩ + chuẩn hoá). Chỉ chạy khi đặt
// TC_BENCH_RENDER=1 (bản Release): docs/record-results.md.
TEST(RenderBench, FourMinuteTake) {
  if (std::getenv("TC_BENCH_RENDER") == nullptr) GTEST_SKIP() << "đặt TC_BENCH_RENDER=1";
  std::vector<float> mic(static_cast<std::size_t>(kRate) * 240);
  for (std::size_t i = 0; i < mic.size(); ++i) {
    const double t = static_cast<double>(i) / kRate;
    const double f =
        220.0 * std::pow(2.0, std::floor(std::fmod(t, 8.0)) / 12.0);  // đổi nốt mỗi giây
    mic[i] = static_cast<float>(0.2 * std::sin(2.0 * kPi * f * t));
  }
  TakeFixture take("render_bench", mic, true);
  tc_render_config cfg;
  tc_render_config_init(&cfg);
  tc_fx_preset_init(&cfg.fx, TC_GENRE_BOLERO);
  cfg.correction_enabled = 1;
  cfg.correction_preset = TC_PRESET_SINGER;
  cfg.voice_offset_frames = 650;
  const auto t0 = std::chrono::steady_clock::now();
  const tc_render_status s = render(take.beat(), take.dry(), take.dir + "/mix.wav", cfg);
  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  ASSERT_EQ(s.state, TC_ANALYSIS_DONE);
  std::printf("render 4 phút: %.2f s (%.0f× thời gian thực), %.1f LUFS, %.2f dBTP\n", sec,
              240.0 / sec, static_cast<double>(s.output_lufs), static_cast<double>(s.true_peak_db));
}
