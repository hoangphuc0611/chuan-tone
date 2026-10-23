#include "key/song_analyzer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "test_util.h"
#include "util/synth_music.h"

using namespace tonecore;

namespace {

DecodedAudio makeAudio(std::vector<int16_t> pcm, uint32_t rate, uint32_t channels) {
  DecodedAudio a;
  a.frames = pcm.size() / channels;
  a.samples = std::move(pcm);
  a.sampleRate = rate;
  a.channels = channels;
  return a;
}

// Tần số chủ đạo ước lượng bằng số lần cắt qua 0 (đủ cho sin sạch).
double zeroCrossingFreq(const std::vector<float>& x, double rate) {
  int crossings = 0;
  for (std::size_t i = 1; i < x.size(); ++i) {
    if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
  }
  return 0.5 * crossings * rate / static_cast<double>(x.size());
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double acc = 0.0;
  for (std::size_t i = from; i < to; ++i) acc += static_cast<double>(x[i]) * x[i];
  return std::sqrt(acc / static_cast<double>(to - from));
}

}  // namespace

TEST(SongAnalyzer, DecimationFactor) {
  EXPECT_EQ(analysisDecimation(48000.0, 11025.0), 4);  // → 12 kHz
  EXPECT_EQ(analysisDecimation(44100.0, 11025.0), 4);  // → 11025 Hz
  EXPECT_EQ(analysisDecimation(22050.0, 11025.0), 2);
  EXPECT_EQ(analysisDecimation(16000.0, 11025.0), 1);
  EXPECT_EQ(analysisDecimation(8000.0, 11025.0), 1);
}

// Stereo 44,1 kHz → mono 11025 Hz: giữ tần số, biên độ, độ dài; giải phóng PCM gốc.
TEST(SongAnalyzer, PrepareSignalResamplesToMono) {
  const uint32_t rate = 44100;
  auto audio = makeAudio(test::sineInt16(440.0, rate, rate * 2, 2, 0.5), rate, 2);
  std::vector<float> progress;
  AnalysisSignal out;
  ASSERT_EQ(prepareAnalysisSignal(
                audio, 11025.0,
                [&](float p) {
                  progress.push_back(p);
                  return true;
                },
                out),
            TC_OK);
  EXPECT_DOUBLE_EQ(out.rate, 11025.0);
  const std::vector<float>& sig = out.samples;
  EXPECT_EQ(sig.size(), 22050u);
  EXPECT_NEAR(zeroCrossingFreq(sig, 11025.0), 440.0, 2.0);
  // Bỏ 0,1 s hai đầu (đáp ứng quá độ của bộ lọc ở biên).
  EXPECT_NEAR(rms(sig, 1100, sig.size() - 1100), 0.5 / std::sqrt(2.0), 0.005);
  EXPECT_TRUE(audio.samples.empty());
  EXPECT_EQ(audio.samples.capacity(), 0u);

  ASSERT_FALSE(progress.empty());
  EXPECT_FLOAT_EQ(progress.back(), 1.0f);
  for (std::size_t i = 1; i < progress.size(); ++i) EXPECT_GT(progress[i], progress[i - 1]);
}

// Trộn mono là trung bình các kênh: L = s, R = −s → im lặng; 3 kênh lấy trung bình.
TEST(SongAnalyzer, MonoMixAveragesChannels) {
  const std::size_t frames = 4800;
  std::vector<int16_t> pcm(frames * 3);
  for (std::size_t i = 0; i < frames; ++i) {
    pcm[3 * i] = 9000;
    pcm[3 * i + 1] = -9000;
    pcm[3 * i + 2] = 3000;
  }
  auto audio = makeAudio(std::move(pcm), 48000, 3);
  AnalysisSignal out;
  ASSERT_EQ(prepareAnalysisSignal(audio, 48000.0, [](float) { return true; }, out), TC_OK);
  ASSERT_EQ(out.samples.size(), frames);
  for (const float v : out.samples) EXPECT_FLOAT_EQ(v, 1000.0f / 32768.0f);
}

// 48 kHz → 12 kHz: 1 kHz đi qua nguyên vẹn; 7 kHz (trên Nyquist mới 6 kHz, sẽ gập về 5 kHz)
// bị chặn ≥ 55 dB.
TEST(SongAnalyzer, DecimationRejectsAliases) {
  auto level = [](double freq) {
    auto audio = makeAudio(test::sineInt16(freq, 48000, 48000, 1, 0.5), 48000, 1);
    AnalysisSignal out;
    EXPECT_EQ(prepareAnalysisSignal(audio, 11025.0, [](float) { return true; }, out), TC_OK);
    EXPECT_DOUBLE_EQ(out.rate, 12000.0);
    return rms(out.samples, 1200, out.samples.size() - 1200) / (0.5 / std::sqrt(2.0));
  };
  EXPECT_NEAR(level(1000.0), 1.0, 0.01);
  EXPECT_LT(20.0 * std::log10(level(7000.0)), -55.0);
}

TEST(SongAnalyzer, PrepareSignalCanBeCancelled) {
  auto audio = makeAudio(test::sineInt16(440.0, 48000, 48000 * 5, 1), 48000, 1);
  AnalysisSignal out;
  int calls = 0;
  EXPECT_EQ(prepareAnalysisSignal(
                audio, 11025.0,
                [&](float) {
                  ++calls;
                  return calls < 2;
                },
                out),
            kResultCancelled);
  EXPECT_EQ(calls, 2);
  EXPECT_TRUE(out.samples.empty());
}

TEST(SongAnalyzer, AnalyzeDecodedFillsBasicInfo) {
  auto audio = makeAudio(test::sineInt16(220.0, 48000, 48000 * 3, 2), 48000, 2);
  AnalysisControl ctl;
  SongAnalysis a;
  ASSERT_EQ(analyzeDecoded(std::move(audio), AnalysisConfig{}, ctl, a), TC_OK);
  EXPECT_DOUBLE_EQ(a.durationSec, 3.0);
  EXPECT_EQ(a.sourceRate, 48000u);
  EXPECT_EQ(a.channels, 2u);
  EXPECT_DOUBLE_EQ(a.analysisRate, 12000.0);
  EXPECT_EQ(ctl.stage(), AnalysisStage::Done);
  EXPECT_FLOAT_EQ(ctl.progress(), 1.0f);
  ASSERT_FALSE(a.timings.empty());
  EXPECT_EQ(a.timings.back().stage, AnalysisStage::Key);
  // Sin 220 Hz đơn lẻ: dưới dải đỉnh dùng để đo tuning (400–3000 Hz) → không có tuning.
  EXPECT_FALSE(a.tuningCents.has_value());
  EXPECT_NEAR(static_cast<double>(a.chroma.frames()), 3.0 / a.chroma.frameSec, 1.0);
  // Có tone thì timeline phủ kín bài (ở đây không có chuyển tone → 1 đoạn); chưa có BPM (B6).
  if (a.key) {
    ASSERT_EQ(a.timeline.size(), 1u);
    EXPECT_EQ(a.timeline[0].key, *a.key);
    EXPECT_DOUBLE_EQ(a.timeline[0].endSec, 3.0);
  } else {
    EXPECT_TRUE(a.timeline.empty());
  }
  EXPECT_FALSE(a.bpm.has_value());
}

// Cả chuỗi trên nhạc tổng hợp: beat lệch +25 cent, hợp âm La thứ (bass A2) + trống →
// tuning ±3 cent, chroma bass nổi La, treble nổi La – Đô – Mi.
TEST(SongAnalyzer, PipelineEstimatesTuningAndChroma) {
  const double rate = 48000.0;
  std::vector<float> x(static_cast<std::size_t>(rate * 12), 0.0f);
  test::NoteSpec spec;
  spec.tuningCents = 25.0;
  for (const int m : {45, 57, 60, 64, 69, 72}) test::addNote(x, rate, 0.0, 12.0, m, 0.06f, spec);
  for (int i = 0; i < 24; ++i) test::addClick(x, rate, 0.5 * i, 0.4f, 50 + i);
  std::vector<int16_t> pcm(x.size() * 2);
  for (std::size_t i = 0; i < x.size(); ++i) {
    pcm[2 * i] = pcm[2 * i + 1] = static_cast<int16_t>(std::lround(x[i] * 32767.0f));
  }
  AnalysisControl ctl;
  SongAnalysis a;
  ASSERT_EQ(analyzeDecoded(makeAudio(std::move(pcm), 48000, 2), AnalysisConfig{}, ctl, a), TC_OK);
  ASSERT_TRUE(a.tuningCents.has_value());
  EXPECT_NEAR(*a.tuningCents, 25.0f, 3.0f);
  ASSERT_GT(a.chroma.frames(), 10u);
  ChromaVector bass{};
  ChromaVector treble{};
  for (std::size_t i = 0; i < a.chroma.frames(); ++i) {
    for (std::size_t pc = 0; pc < 12; ++pc) {
      bass[pc] += a.chroma.bass[i][pc];
      treble[pc] += a.chroma.treble[i][pc];
    }
  }
  EXPECT_EQ(std::max_element(bass.begin(), bass.end()) - bass.begin(), 9);
  std::vector<std::size_t> order(12);
  for (std::size_t pc = 0; pc < 12; ++pc) order[pc] = pc;
  std::sort(order.begin(), order.end(),
            [&](std::size_t l, std::size_t r) { return treble[l] > treble[r]; });
  std::vector<std::size_t> top(order.begin(), order.begin() + 3);
  std::sort(top.begin(), top.end());
  EXPECT_EQ(top, (std::vector<std::size_t>{0, 4, 9}));
}

TEST(SongAnalyzer, RejectsEmptyAudio) {
  AnalysisControl ctl;
  SongAnalysis a;
  EXPECT_EQ(analyzeDecoded(DecodedAudio{}, AnalysisConfig{}, ctl, a), TC_ERR_INVALID_ARG);
}

TEST(SongAnalyzer, CancelBeforeStart) {
  auto audio = makeAudio(test::sineInt16(220.0, 48000, 48000, 1), 48000, 1);
  AnalysisControl ctl;
  ctl.cancel();
  SongAnalysis a;
  EXPECT_EQ(analyzeDecoded(std::move(audio), AnalysisConfig{}, ctl, a), kResultCancelled);
}

// Huỷ từ luồng khác trong lúc đang chạy (TSan kiểm tra AnalysisControl không có data race).
TEST(SongAnalyzer, CancelFromAnotherThread) {
  auto audio = makeAudio(test::sineInt16(220.0, 48000, 48000 * 60, 2), 48000, 2);
  AnalysisControl ctl;
  std::thread canceller([&] {
    while (ctl.progress() <= 0.3f) std::this_thread::yield();
    ctl.cancel();
  });
  SongAnalysis a;
  const tc_result r = analyzeDecoded(std::move(audio), AnalysisConfig{}, ctl, a);
  canceller.join();
  // Máy rất nhanh có thể xong trước khi kịp huỷ; cả hai đều hợp lệ, nhưng không được lỗi khác.
  EXPECT_TRUE(r == kResultCancelled || r == TC_OK) << r;
}

TEST(SongAnalyzer, AnalyzeFileReadsWav) {
  const std::string path = test::tempPath("analyze.wav");
  ASSERT_TRUE(test::writeWav16(path, test::sineInt16(330.0, 44100, 44100 * 2, 2), 44100, 2));
  AnalysisControl ctl;
  SongAnalysis a;
  ASSERT_EQ(analyzeFile(path, AnalysisConfig{}, ctl, a), TC_OK);
  EXPECT_DOUBLE_EQ(a.durationSec, 2.0);
  EXPECT_EQ(a.sourceRate, 44100u);
  ASSERT_GE(a.timings.size(), 2u);
  EXPECT_EQ(a.timings.front().stage, AnalysisStage::Decoding);
  std::remove(path.c_str());
}

TEST(SongAnalyzer, AnalyzeFileMissing) {
  AnalysisControl ctl;
  SongAnalysis a;
  EXPECT_EQ(analyzeFile("/khong/ton/tai.mp3", AnalysisConfig{}, ctl, a), TC_ERR_INVALID_ARG);
}

// Đầu-cuối: beat 48 kHz stereo lệch −35 cent, vòng hoà âm + trống to → đúng tone, tuning ±3 cent.
TEST(SongAnalyzer, PipelineDetectsKeyWithDrumsAndDetune) {
  const double rate = 48000.0;
  const MusicalKey keys[] = {{6, KeyMode::Minor}, {10, KeyMode::Major}, {4, KeyMode::Major}};
  for (const MusicalKey& truth : keys) {
    SCOPED_TRACE(truth.root);
    std::vector<float> x(static_cast<std::size_t>(rate * 12), 0.0f);
    test::NoteSpec spec;
    spec.tuningCents = -35.0;
    spec.decayPerSec = 0.8;
    test::addCadence(x, rate, 0.0, truth.root, truth.mode == KeyMode::Minor, 2, 1.5,
                     static_cast<uint32_t>(truth.root), spec);
    for (int i = 0; i < 24; ++i) test::addClick(x, rate, 0.5 * i, 0.5f, 300 + i);
    std::vector<int16_t> pcm(x.size() * 2);
    for (std::size_t i = 0; i < x.size(); ++i) {
      const float v = std::clamp(x[i], -1.0f, 1.0f);
      pcm[2 * i] = pcm[2 * i + 1] = static_cast<int16_t>(std::lround(v * 32767.0f));
    }
    AnalysisControl ctl;
    SongAnalysis a;
    ASSERT_EQ(analyzeDecoded(makeAudio(std::move(pcm), 48000, 2), AnalysisConfig{}, ctl, a), TC_OK);
    ASSERT_TRUE(a.key.has_value());
    EXPECT_EQ(*a.key, truth) << "đoán " << a.key->root
                             << (a.key->mode == KeyMode::Minor ? "m" : "");
    EXPECT_GT(a.keyConfidence, 0.3f);
    ASSERT_TRUE(a.tuningCents.has_value());
    EXPECT_NEAR(*a.tuningCents, -35.0f, 3.0f);
    EXPECT_EQ(a.timings.back().stage, AnalysisStage::Key);
  }
}

// Đầu-cuối: Am 48 s → A trưởng 42 s, có trống, 48 kHz → timeline 2 đoạn, biên lệch ≤ 4 s.
TEST(SongAnalyzer, PipelineTracksModulation) {
  const double rate = 48000.0;
  std::vector<float> x(static_cast<std::size_t>(rate * 90), 0.0f);
  test::NoteSpec spec;
  spec.harmonics = 4;
  test::addCadence(x, rate, 0.0, 9, true, 8, 1.5, 11, spec);
  test::addCadence(x, rate, 48.0, 9, false, 7, 1.5, 12, spec);
  for (int i = 0; i < 180; ++i) test::addClick(x, rate, 0.5 * i, 0.4f, 700 + i);
  std::vector<int16_t> pcm(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) {
    pcm[i] = static_cast<int16_t>(std::lround(std::clamp(x[i], -1.0f, 1.0f) * 32767.0f));
  }
  AnalysisControl ctl;
  SongAnalysis a;
  ASSERT_EQ(analyzeDecoded(makeAudio(std::move(pcm), 48000, 1), AnalysisConfig{}, ctl, a), TC_OK);
  ASSERT_EQ(a.timeline.size(), 2u);
  EXPECT_EQ(a.timeline[0].key, (MusicalKey{9, KeyMode::Minor}));
  EXPECT_EQ(a.timeline[1].key, (MusicalKey{9, KeyMode::Major}));
  EXPECT_NEAR(a.timeline[1].startSec, 48.0, 4.0);
  EXPECT_DOUBLE_EQ(a.timeline.back().endSec, 90.0);
}
