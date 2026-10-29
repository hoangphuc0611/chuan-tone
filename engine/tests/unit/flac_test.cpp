// Bộ nén FLAC tự viết (RFC 9639) + C API xuất FLAC theo đoạn (v0.7, M6 B2). Mỗi file nén ra được
// giải mã lại bằng dr_flac và (trên Apple) bằng ExtAudioFile của hệ điều hành: phải khớp từng mẫu.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "codec/flac_encoder.h"
#include "decode/decoder.h"
#include "dsp/resampler.h"
#include "test_util.h"
#include "tonecore/tonecore.h"

using namespace tonecore;

namespace {

// Nhạc giả: vài nốt có hoà âm, trống (nhiễu tắt dần), một đoạn im, mép cực đại / cực tiểu.
std::vector<int16_t> musicLike(uint64_t frames, uint32_t channels, uint32_t rate, uint32_t seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> noise(0.0, 1.0);
  std::vector<int16_t> out(frames * channels);
  double drum = 0.0;
  for (uint64_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / rate;
    if (i % (rate / 2) == 0) drum = 1.0;
    drum *= 0.9995;
    const bool silent = t > 0.30 && t < 0.35;
    for (uint32_t c = 0; c < channels; ++c) {
      double v = 0.0;
      if (!silent) {
        const double f = 220.0 * (1.0 + 0.5 * c);
        v = 0.3 * std::sin(2 * M_PI * f * t) + 0.12 * std::sin(2 * M_PI * 2.01 * f * t) +
            0.05 * std::sin(2 * M_PI * 3.3 * f * t) + 0.25 * drum * noise(rng) + 0.002 * noise(rng);
      }
      const double s = std::round(v * 32767.0);
      out[i * channels + c] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, s)));
    }
  }
  // Mép: cặp cực đại / cực tiểu (kênh hiệu cần đủ 17 bit).
  if (frames > 10 && channels == 2) {
    for (uint64_t i = 0; i < 10; ++i) {
      out[i * 2] = (i % 2) ? 32767 : -32768;
      out[i * 2 + 1] = (i % 2) ? -32768 : 32767;
    }
  }
  return out;
}

void writeBytes(const std::string& path, const std::vector<uint8_t>& data) {
  ASSERT_EQ(writeFileAtomic(path, data), TC_OK);
}

// Giải mã bằng mọi bộ giải mã có trên máy, so từng mẫu.
void expectDecodesTo(const std::string& path, const std::vector<int16_t>& pcm, uint32_t channels,
                     uint32_t rate) {
  DecodedAudio a;
  ASSERT_EQ(decodeFileDrLibs(path, a), TC_OK) << path;
  EXPECT_EQ(a.channels, channels);
  EXPECT_EQ(a.sampleRate, rate);
  ASSERT_EQ(a.samples.size(), pcm.size());
  EXPECT_TRUE(a.samples == pcm) << "dr_flac lệch mẫu";
#ifdef __APPLE__
  DecodedAudio b;
  ASSERT_EQ(decodeFile(path, b), TC_OK) << "ExtAudioFile không mở được " << path;
  EXPECT_EQ(b.channels, channels);
  EXPECT_EQ(b.sampleRate, rate);
  ASSERT_EQ(b.samples.size(), pcm.size());
  EXPECT_TRUE(b.samples == pcm) << "ExtAudioFile lệch mẫu";
#endif
}

void roundTrip(const std::vector<int16_t>& pcm, uint32_t channels, uint32_t rate,
               const std::string& name) {
  const auto frames = pcm.size() / channels;
  const auto data = encodeFlac(pcm.data(), frames, channels, rate);
  const std::string path = test::tempPath(name);
  writeBytes(path, data);
  expectDecodesTo(path, pcm, channels, rate);
  std::remove(path.c_str());
}

}  // namespace

TEST(FlacEncoder, StereoMusicRoundTripsExactlyAndCompresses) {
  const uint64_t frames = 3 * 4096 * 7 + 1234;  // khối cuối ngắn
  const auto pcm = musicLike(frames, 2, 44100, 1);
  const auto data = encodeFlac(pcm.data(), frames, 2, 44100);
  const double ratio = static_cast<double>(data.size()) / (static_cast<double>(pcm.size()) * 2.0);
  EXPECT_LT(ratio, 0.75) << "nén kém: " << ratio;
  roundTrip(pcm, 2, 44100, "flac_stereo.flac");
}

TEST(FlacEncoder, MonoAndOddRatesRoundTrip) {
  roundTrip(musicLike(50000, 1, 48000, 2), 1, 48000, "flac_mono48.flac");
  roundTrip(musicLike(20000, 2, 22050, 3), 2, 22050, "flac_22k.flac");
  // Tần số không có mã riêng: ghi thẳng Hz (16 bit) / kHz (8 bit) / chục Hz (16 bit) trong khung.
  roundTrip(musicLike(20000, 2, 37800, 4), 2, 37800, "flac_37k8.flac");
  roundTrip(musicLike(20000, 1, 11025, 12), 1, 11025, "flac_11k.flac");
  roundTrip(musicLike(20000, 1, 64000, 13), 1, 64000, "flac_64k.flac");
  roundTrip(musicLike(20000, 1, 352800, 14), 1, 352800, "flac_352k8.flac");
}

TEST(FlacEncoder, ShortAndConstantBlocks) {
  roundTrip(musicLike(1, 2, 44100, 5), 2, 44100, "flac_1.flac");
  roundTrip(musicLike(100, 2, 44100, 6), 2, 44100, "flac_100.flac");
  roundTrip(musicLike(4096, 2, 44100, 7), 2, 44100, "flac_4096.flac");
  std::vector<int16_t> dc(2 * 9000, -1234);
  roundTrip(dc, 2, 44100, "flac_dc.flac");
  const std::vector<int16_t> silence(2 * 44100 * 10, 0);
  const auto data = encodeFlac(silence.data(), 44100 * 10, 2, 44100);
  EXPECT_LT(data.size(), 2000u);  // 10 s im lặng: chỉ còn các khung CONSTANT
}

TEST(FlacEncoder, WhiteNoiseFallsBackToVerbatim) {
  std::mt19937 rng(8);
  std::uniform_int_distribution<int> d(-32768, 32767);
  std::vector<int16_t> pcm(2 * 20000);
  for (auto& v : pcm) v = static_cast<int16_t>(d(rng));
  const auto data = encodeFlac(pcm.data(), 20000, 2, 44100);
  EXPECT_LT(data.size(), pcm.size() * 2 + pcm.size() / 50);  // không phình quá 1%
  roundTrip(pcm, 2, 44100, "flac_noise.flac");
}

TEST(FlacEncoder, EmptyStreamAndBadArgs) {
  const auto data = encodeFlac(nullptr, 0, 2, 44100);
  EXPECT_EQ(data.size(), 42u);  // "fLaC" + STREAMINFO
  const int16_t x = 0;
  EXPECT_THROW(encodeFlac(&x, 1, 0, 44100), std::invalid_argument);
  EXPECT_THROW(encodeFlac(&x, 1, 9, 44100), std::invalid_argument);
  EXPECT_THROW(encodeFlac(&x, 1, 1, 0), std::invalid_argument);
  EXPECT_THROW(encodeFlac(nullptr, 5, 1, 44100), std::invalid_argument);
}

namespace {

tc_export_status waitExport(tc_exporter* e) {
  tc_export_status s{};
  s.struct_size = sizeof(s);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (;;) {
    EXPECT_EQ(tc_exporter_get_status(e, &s), TC_OK);
    if (s.state != TC_ANALYSIS_RUNNING) return s;
    if (std::chrono::steady_clock::now() > deadline) {
      ADD_FAILURE() << "xuất FLAC quá lâu";
      return s;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

std::vector<int16_t> concatChunks(const std::string& prefix, uint32_t count) {
  std::vector<int16_t> all;
  for (uint32_t i = 0; i < count; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "_%03u.flac", i);
    DecodedAudio a;
    EXPECT_EQ(decodeFileDrLibs(prefix + name, a), TC_OK) << prefix + name;
    all.insert(all.end(), a.samples.begin(), a.samples.end());
    std::remove((prefix + name).c_str());
  }
  return all;
}

}  // namespace

TEST(FlacExportApi, ChunksConcatenateToDecodedPcm) {
  const uint32_t rate = 44100;
  const uint64_t frames = rate * 10 + 777;
  const auto pcm = musicLike(frames, 2, rate, 9);
  const std::string wav = test::tempPath("export_src.wav");
  ASSERT_TRUE(test::writeWav16(wav, pcm, rate, 2));
  const std::string prefix = test::tempPath("export_out");

  tc_exporter* e = nullptr;
  ASSERT_EQ(tc_exporter_create(&e), TC_OK);
  tc_export_config cfg;
  tc_export_config_init(&cfg);
  EXPECT_EQ(cfg.struct_size, sizeof(cfg));
  cfg.chunk_seconds = 3.0f;
  ASSERT_EQ(tc_exporter_start(e, wav.c_str(), prefix.c_str(), &cfg), TC_OK);
  const auto s = waitExport(e);
  ASSERT_EQ(s.state, TC_ANALYSIS_DONE) << s.result;
  EXPECT_EQ(s.chunks_total, 4u);  // 3 + 3 + 3 + 1,02 s
  EXPECT_EQ(s.chunks_done, 4u);
  EXPECT_EQ(s.sample_rate, rate);
  EXPECT_EQ(s.channels, 2u);
  EXPECT_EQ(s.frames, frames);
  EXPECT_EQ(s.chunk_frames, 3u * rate);
  EXPECT_FLOAT_EQ(s.progress, 1.0f);
  EXPECT_TRUE(concatChunks(prefix, 4) == pcm);

  // Một file, đổi sang 48 kHz: đúng bằng Resampler của engine.
  cfg.chunk_seconds = 0.0f;
  cfg.sample_rate = 48000;
  ASSERT_EQ(tc_exporter_start(e, wav.c_str(), prefix.c_str(), &cfg), TC_OK);
  const auto s2 = waitExport(e);
  ASSERT_EQ(s2.state, TC_ANALYSIS_DONE);
  EXPECT_EQ(s2.chunks_total, 1u);
  const auto expected = Resampler(rate, 48000).process(pcm.data(), frames, 2);
  EXPECT_EQ(s2.frames, expected.size() / 2);
  DecodedAudio a;
  ASSERT_EQ(decodeFileDrLibs(prefix + ".flac", a), TC_OK);
  EXPECT_EQ(a.sampleRate, 48000u);
  EXPECT_TRUE(a.samples == expected);
  std::remove((prefix + ".flac").c_str());

  tc_exporter_destroy(e);
  std::remove(wav.c_str());
}

TEST(FlacExportApi, ErrorsAndLimits) {
  tc_exporter* e = nullptr;
  ASSERT_EQ(tc_exporter_create(&e), TC_OK);
  tc_export_config cfg;
  tc_export_config_init(&cfg);
  const std::string prefix = test::tempPath("export_err");
  EXPECT_EQ(tc_exporter_start(e, "", prefix.c_str(), &cfg), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_exporter_start(e, "/a.wav", nullptr, &cfg), TC_ERR_INVALID_ARG);
  tc_export_config bad = cfg;
  bad.struct_size = 4;
  EXPECT_EQ(tc_exporter_start(e, "/a.wav", prefix.c_str(), &bad), TC_ERR_INVALID_ARG);
  bad = cfg;
  bad.chunk_seconds = std::nanf("");
  EXPECT_EQ(tc_exporter_start(e, "/a.wav", prefix.c_str(), &bad), TC_ERR_INVALID_ARG);

  ASSERT_EQ(tc_exporter_start(e, "/khong/ton/tai.wav", prefix.c_str(), &cfg), TC_OK);
  auto s = waitExport(e);
  EXPECT_EQ(s.state, TC_ANALYSIS_FAILED);
  EXPECT_EQ(s.result, TC_ERR_INVALID_ARG);

  // Bài 2 s, giới hạn 1 s → không hỗ trợ.
  const std::string wav = test::tempPath("export_long.wav");
  ASSERT_TRUE(test::writeWav16(wav, musicLike(88200, 2, 44100, 10), 44100, 2));
  cfg.max_seconds = 1.0f;
  ASSERT_EQ(tc_exporter_start(e, wav.c_str(), prefix.c_str(), &cfg), TC_OK);
  s = waitExport(e);
  EXPECT_EQ(s.state, TC_ANALYSIS_FAILED);
  EXPECT_EQ(s.result, TC_ERR_NOT_SUPPORTED);

  // Thư mục đích không có → lỗi ghi file.
  cfg.max_seconds = 0.0f;
  ASSERT_EQ(tc_exporter_start(e, wav.c_str(), "/khong/ton/tai/x", &cfg), TC_OK);
  s = waitExport(e);
  EXPECT_EQ(s.state, TC_ANALYSIS_FAILED);

  tc_export_status st{};
  st.struct_size = 3;
  EXPECT_EQ(tc_exporter_get_status(e, &st), TC_ERR_INVALID_ARG);
  tc_exporter_destroy(e);
  tc_exporter_destroy(nullptr);
  std::remove(wav.c_str());
}

TEST(FlacExportApi, CancelKeepsFinishedChunks) {
  const uint32_t rate = 44100;
  const std::string wav = test::tempPath("export_cancel.wav");
  ASSERT_TRUE(test::writeWav16(wav, musicLike(rate * 60, 2, rate, 11), rate, 2));
  const std::string prefix = test::tempPath("export_cancel");
  tc_exporter* e = nullptr;
  ASSERT_EQ(tc_exporter_create(&e), TC_OK);
  tc_export_config cfg;
  tc_export_config_init(&cfg);
  cfg.chunk_seconds = 1.0f;
  ASSERT_EQ(tc_exporter_start(e, wav.c_str(), prefix.c_str(), &cfg), TC_OK);
  EXPECT_EQ(tc_exporter_start(e, wav.c_str(), prefix.c_str(), &cfg), TC_ERR_INVALID_STATE);
  tc_export_status s{};
  s.struct_size = sizeof(s);
  do {  // chờ có ít nhất một đoạn rồi huỷ
    ASSERT_EQ(tc_exporter_get_status(e, &s), TC_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (s.state == TC_ANALYSIS_RUNNING && s.chunks_done == 0);
  ASSERT_EQ(tc_exporter_cancel(e), TC_OK);
  s = waitExport(e);
  if (s.state == TC_ANALYSIS_CANCELLED) {
    EXPECT_LT(s.chunks_done, s.chunks_total);
    EXPECT_EQ(s.result, TC_ERR_CANCELLED);
  } else {
    EXPECT_EQ(s.state, TC_ANALYSIS_DONE);  // máy quá nhanh: xong trước khi kịp huỷ
  }
  concatChunks(prefix, s.chunks_done);  // các đoạn đã ghi đọc được
  tc_exporter_destroy(e);
  std::remove(wav.c_str());
}
