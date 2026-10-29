// Bài gốc + stem giọng (M6 B4): decodeStems (phần nhạc = bài − giọng), phát phần nhạc + k · giọng
// qua C API (TC_NODE_VOCAL), đổi tone đổi cả giọng, phân tích trên phần nhạc.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "codec/flac_encoder.h"
#include "decode/stems.h"
#include "dsp/resampler.h"
#include "test_util.h"
#include "tonecore/tonecore.h"

using namespace tonecore;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;

std::vector<int16_t> sine(double hz, uint32_t rate, std::size_t frames, double amp) {
  return test::sineInt16(hz, rate, frames, 2, amp);
}

std::vector<int16_t> add(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  std::vector<int16_t> out(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    out[i] = static_cast<int16_t>(static_cast<int>(a[i]) + static_cast<int>(b[i]));
  }
  return out;
}

// Ghi `pcm` stereo thành các đoạn FLAC `chunkFrames` mẫu (như server trả về), trả đường dẫn.
std::vector<std::string> writeChunks(const std::vector<int16_t>& pcm, uint32_t rate,
                                     uint64_t chunkFrames, const std::string& name) {
  std::vector<std::string> paths;
  const uint64_t frames = pcm.size() / 2;
  for (uint64_t start = 0, i = 0; start < frames; start += chunkFrames, ++i) {
    const uint64_t n = std::min(chunkFrames, frames - start);
    const std::string p = test::tempPath(name + "_" + std::to_string(i) + ".flac");
    EXPECT_EQ(writeFileAtomic(p, encodeFlac(pcm.data() + 2 * start, n, 2, rate)), TC_OK);
    paths.push_back(p);
  }
  return paths;
}

// Mức (RMS tương đương) của thành phần tần số `hz` ở kênh trái — Goertzel.
double toneLevel(const std::vector<float>& stereo, double hz, double rate) {
  const std::size_t n = stereo.size() / 2;
  const double w = 2.0 * M_PI * hz / rate;
  double s1 = 0.0;
  double s2 = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double win =
        0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n));
    const double s = stereo[2 * i] * win + 2.0 * std::cos(w) * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  const double power = s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2;
  return std::sqrt(power) * 2.0 / (0.5 * static_cast<double>(n));
}

}  // namespace

TEST(Stems, InstrumentalIsMixMinusVocalExactly) {
  const std::size_t frames = 44100 * 3 + 333;
  const auto inst = sine(300.0, 44100, frames, 0.3);
  const auto vocal = sine(1000.0, 44100, frames, 0.25);
  const std::string mixPath = test::tempPath("stems_mix.wav");
  ASSERT_TRUE(test::writeWav16(mixPath, add(inst, vocal), 44100, 2));
  const auto chunks = writeChunks(vocal, 44100, 44100, "stems_v");
  ASSERT_EQ(chunks.size(), 4u);

  DecodedAudio i, v;
  ASSERT_EQ(decodeStems(mixPath, chunks, i, v), TC_OK);
  EXPECT_EQ(i.sampleRate, 44100u);
  EXPECT_EQ(i.frames, frames);
  EXPECT_TRUE(i.samples == inst);
  EXPECT_TRUE(v.samples == vocal);

  // Lỗi: thiếu đoạn (lệch > kMaxStemLengthSlack), không có đoạn nào, đoạn không đọc được.
  EXPECT_EQ(decodeStems(mixPath, {chunks[0], chunks[1]}, i, v), TC_ERR_INVALID_ARG);
  EXPECT_EQ(decodeStems(mixPath, {}, i, v), TC_ERR_INVALID_ARG);
  EXPECT_NE(decodeStems(mixPath, {"/khong/co.flac"}, i, v), TC_OK);
  // Giọng mono, bài stereo.
  std::vector<int16_t> mono(frames);
  const std::string monoPath = test::tempPath("stems_mono.flac");
  ASSERT_EQ(writeFileAtomic(monoPath, encodeFlac(mono.data(), frames, 1, 44100)), TC_OK);
  EXPECT_EQ(decodeStems(mixPath, {monoPath}, i, v), TC_ERR_INVALID_ARG);
  for (const auto& p : chunks) std::remove(p.c_str());
  std::remove(monoPath.c_str());
  std::remove(mixPath.c_str());
}

// Bài 48 kHz gửi lên ở 44,1 kHz: giọng trả về được đổi lại 48 kHz, phần nhạc gần đúng bài − giọng.
TEST(Stems, ResampledVocalStillCancels) {
  const std::size_t frames = kRate * 2;
  const auto inst = sine(300.0, kRate, frames, 0.3);
  const auto vocal = sine(1000.0, kRate, frames, 0.25);
  const std::string mixPath = test::tempPath("stems_mix48.wav");
  ASSERT_TRUE(test::writeWav16(mixPath, add(inst, vocal), kRate, 2));
  const auto v441 = Resampler(kRate, 44100).process(vocal.data(), frames, 2);
  const auto chunks = writeChunks(v441, 44100, 44100, "stems_v48");

  DecodedAudio i, v;
  ASSERT_EQ(decodeStems(mixPath, chunks, i, v), TC_OK);
  ASSERT_EQ(i.frames, frames);
  double err = 0.0;
  double ref = 0.0;
  for (std::size_t k = 4800; k < 2 * frames - 4800; ++k) {  // bỏ hai mép (bộ lọc đổi tần số)
    const double e = static_cast<double>(i.samples[k]) - inst[k];
    err += e * e;
    ref += static_cast<double>(vocal[k]) * vocal[k];
  }
  EXPECT_LT(10.0 * std::log10(err / ref), -50.0) << "giọng còn sót trong phần nhạc";
  for (const auto& p : chunks) std::remove(p.c_str());
  std::remove(mixPath.c_str());
}

namespace {

struct Player {
  tc_engine* e = nullptr;
  std::vector<float> out;

  Player() {
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
  }
  ~Player() { tc_engine_destroy(e); }

  int32_t waitLoaded() {
    tc_event buf[32];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
      const int32_t n = tc_poll_events(e, buf, 32);
      for (int32_t k = 0; k < n; ++k) {
        if (buf[k].type == TC_EVENT_BEAT_LOADED) return buf[k].code;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -999;
  }

  void run(double seconds) {
    std::vector<float> o(2 * kBlock);
    out.clear();
    const auto blocks = static_cast<int>(seconds * kRate / kBlock);
    for (int b = 0; b < blocks; ++b) {
      ASSERT_EQ(tc_engine_process_offline(e, nullptr, o.data(), kBlock), TC_OK);
      out.insert(out.end(), o.begin(), o.end());
    }
  }

  tc_playback_state playback() {
    tc_playback_state s{};
    s.struct_size = sizeof(s);
    EXPECT_EQ(tc_get_playback_state(e, &s), TC_OK);
    return s;
  }
};

}  // namespace

// Phần nhạc 440 Hz, giọng 1 kHz: k = 0 → chỉ có nhạc; k = 1 → như bài gốc; đổi tone +2 → giọng lên
// 1 kHz · 2^(2/12) cùng nhạc; k = 0 vẫn không lọt giọng sau khi đổi tone.
TEST(EngineVocalStem, GuideVocalGainAndTranspose) {
  const std::size_t frames = 44100 * 6;
  const auto inst = sine(440.0, 44100, frames, 0.25);
  const auto vocal = sine(1000.0, 44100, frames, 0.25);
  const std::string mixPath = test::tempPath("ev_mix.wav");
  ASSERT_TRUE(test::writeWav16(mixPath, add(inst, vocal), 44100, 2));
  const auto chunks = writeChunks(vocal, 44100, 44100 * 2, "ev_v");
  std::vector<const char*> cpaths;
  for (const auto& p : chunks) cpaths.push_back(p.c_str());

  Player p;
  EXPECT_EQ(tc_load_beat_with_vocals(p.e, mixPath.c_str(), nullptr, 1), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_load_beat_with_vocals(p.e, mixPath.c_str(), cpaths.data(), 0), TC_ERR_INVALID_ARG);
  ASSERT_EQ(tc_load_beat_with_vocals(p.e, mixPath.c_str(), cpaths.data(),
                                     static_cast<uint32_t>(cpaths.size())),
            TC_OK);
  ASSERT_EQ(p.waitLoaded(), TC_OK);
  EXPECT_EQ(p.playback().has_vocal, 1);
  float g = -1.0f;
  ASSERT_EQ(tc_get_param(p.e, TC_NODE_VOCAL, TC_PARAM_GAIN, &g), TC_OK);
  EXPECT_EQ(g, 0.0f);  // mặc định karaoke

  ASSERT_EQ(tc_play(p.e), TC_OK);
  p.run(0.5);
  const double music0 = toneLevel(p.out, 440.0, kRate);
  const double voice0 = toneLevel(p.out, 1000.0, kRate);
  EXPECT_GT(music0, 0.2);
  EXPECT_LT(voice0, 0.002);

  ASSERT_EQ(tc_set_param(p.e, TC_NODE_VOCAL, TC_PARAM_GAIN, 1.0f), TC_OK);
  p.run(0.2);  // trượt 20 ms
  p.run(0.5);
  EXPECT_NEAR(toneLevel(p.out, 1000.0, kRate), music0, 0.02 * music0);
  // Trượt mượt: không có bước nhảy lớn giữa hai mẫu liền nhau lúc đổi k.
  ASSERT_EQ(tc_set_param(p.e, TC_NODE_VOCAL, TC_PARAM_GAIN, 0.0f), TC_OK);
  p.run(0.1);
  float maxJump = 0.0f;
  for (std::size_t i = 2; i < p.out.size(); i += 2) {
    maxJump = std::max(maxJump, std::abs(p.out[i] - p.out[i - 2]));
  }
  EXPECT_LT(maxJump, 0.12f);  // hai sin 0,25 · 2π·f/fs ≈ 0,05 mỗi cái

  // Đổi tone +2: chờ render xong cả bài (không chạy audio — test nhanh hơn thời gian thực, chạy
  // song song thì phát hết bài trước), rồi chạy tới khi nghe tone mới.
  ASSERT_EQ(tc_set_transpose(p.e, 2), TC_OK);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (p.playback().transpose_progress < 1.0f) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  while (p.playback().audible_transpose != 2) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    p.run(static_cast<double>(kBlock) / kRate);
  }
  const double up = std::pow(2.0, 2.0 / 12.0);
  p.run(0.5);
  EXPECT_GT(toneLevel(p.out, 440.0 * up, kRate), 0.15);
  EXPECT_LT(toneLevel(p.out, 1000.0 * up, kRate), 0.005);
  ASSERT_EQ(tc_set_param(p.e, TC_NODE_VOCAL, TC_PARAM_GAIN, 1.0f), TC_OK);
  p.run(0.2);
  p.run(0.5);
  EXPECT_GT(toneLevel(p.out, 1000.0 * up, kRate), 0.15);
  EXPECT_LT(toneLevel(p.out, 1000.0, kRate), 0.01);  // giọng cũng đã đổi tone

  // Nạp beat thường: hết giọng.
  ASSERT_EQ(tc_load_beat(p.e, mixPath.c_str()), TC_OK);
  ASSERT_EQ(p.waitLoaded(), TC_OK);
  EXPECT_EQ(p.playback().has_vocal, 0);

  for (const auto& c : chunks) std::remove(c.c_str());
  std::remove(mixPath.c_str());
}

TEST(EngineVocalStem, MismatchedStemFailsLoad) {
  const std::size_t frames = 44100 * 2;
  const std::string mixPath = test::tempPath("ev_bad_mix.wav");
  ASSERT_TRUE(test::writeWav16(mixPath, sine(440.0, 44100, frames, 0.25), 44100, 2));
  const auto chunks = writeChunks(sine(1000.0, 44100, frames / 2, 0.25), 44100, 44100, "ev_bad");
  const char* paths[] = {chunks[0].c_str()};
  Player p;
  ASSERT_EQ(tc_load_beat_with_vocals(p.e, mixPath.c_str(), paths, 1), TC_OK);
  EXPECT_EQ(p.waitLoaded(), TC_ERR_INVALID_ARG);
  EXPECT_EQ(p.playback().has_beat, 0);
  for (const auto& c : chunks) std::remove(c.c_str());
  std::remove(mixPath.c_str());
}

// Phân tích bài + giọng = phân tích đúng file phần nhạc.
TEST(AnalyzerVocalStem, AnalyzesInstrumental) {
  const std::size_t frames = 44100 * 12;
  std::vector<int16_t> inst(2 * frames);
  // Hợp âm Đô trưởng ngân + nhịp gõ 120 BPM.
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 44100.0;
    double s = 0.0;
    for (const double hz : {261.63, 329.63, 392.0, 130.81}) s += 0.08 * std::sin(2 * M_PI * hz * t);
    const double beat = std::fmod(t, 0.5);
    s += 0.3 * std::exp(-beat * 40.0) * std::sin(2 * M_PI * 60.0 * t);
    inst[2 * i] = inst[2 * i + 1] = static_cast<int16_t>(std::lround(s * 32767.0));
  }
  const auto vocal = sine(370.0, 44100, frames, 0.2);  // Fa# — lạc tone so với Đô trưởng
  const std::string instPath = test::tempPath("av_inst.wav");
  const std::string mixPath = test::tempPath("av_mix.wav");
  ASSERT_TRUE(test::writeWav16(instPath, inst, 44100, 2));
  ASSERT_TRUE(test::writeWav16(mixPath, add(inst, vocal), 44100, 2));
  const auto chunks = writeChunks(vocal, 44100, 44100 * 5, "av_v");
  std::vector<const char*> cpaths;
  for (const auto& p : chunks) cpaths.push_back(p.c_str());

  auto analyze = [](const std::function<tc_result(tc_analyzer*)>& start) {
    tc_analyzer* a = nullptr;
    EXPECT_EQ(tc_analyzer_create(&a), TC_OK);
    EXPECT_EQ(start(a), TC_OK);
    tc_analysis_status s{};
    s.struct_size = sizeof(s);
    do {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      EXPECT_EQ(tc_analyzer_get_status(a, &s), TC_OK);
    } while (s.state == TC_ANALYSIS_RUNNING);
    EXPECT_EQ(s.state, TC_ANALYSIS_DONE) << s.result;
    tc_analysis_result r{};
    r.struct_size = sizeof(r);
    EXPECT_EQ(tc_analyzer_get_result(a, &r), TC_OK);
    tc_analyzer_destroy(a);
    return r;
  };
  const auto direct =
      analyze([&](tc_analyzer* a) { return tc_analyzer_start(a, instPath.c_str()); });
  const auto stems = analyze([&](tc_analyzer* a) {
    return tc_analyzer_start_with_vocals(a, mixPath.c_str(), cpaths.data(),
                                         static_cast<uint32_t>(cpaths.size()));
  });
  EXPECT_EQ(stems.has_key, direct.has_key);
  EXPECT_EQ(stems.key_root, direct.key_root);
  EXPECT_EQ(stems.key_scale, direct.key_scale);
  EXPECT_EQ(stems.has_bpm, direct.has_bpm);
  EXPECT_FLOAT_EQ(stems.bpm, direct.bpm);
  EXPECT_FLOAT_EQ(stems.integrated_lufs, direct.integrated_lufs);

  tc_analyzer* a = nullptr;
  ASSERT_EQ(tc_analyzer_create(&a), TC_OK);
  EXPECT_EQ(tc_analyzer_start_with_vocals(a, mixPath.c_str(), nullptr, 1), TC_ERR_INVALID_ARG);
  tc_analyzer_destroy(a);
  for (const auto& c : chunks) std::remove(c.c_str());
  std::remove(instPath.c_str());
  std::remove(mixPath.c_str());
}
