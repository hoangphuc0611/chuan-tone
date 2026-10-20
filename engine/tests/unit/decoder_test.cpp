#include "decode/decoder.h"

#include <gtest/gtest.h>

#include <cstdio>

#include "player/track.h"
#include "test_util.h"

using namespace tonecore;

TEST(Decoder, MissingFileIsInvalidArg) {
  DecodedAudio a;
  EXPECT_EQ(decodeFile("/khong/ton/tai.wav", a), TC_ERR_INVALID_ARG);
  EXPECT_EQ(a.frames, 0u);
}

TEST(Decoder, GarbageFileIsRejected) {
  const std::string path = test::tempPath("garbage.wav");
  FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  std::fputs("day khong phai file am thanh", f);
  std::fclose(f);
  DecodedAudio a;
  EXPECT_NE(decodeFile(path, a), TC_OK);
  std::remove(path.c_str());
}

// Bộ giải mã của nền tảng (ExtAudioFile trên macOS, dr_wav trên Linux) phải trả đúng từng mẫu.
TEST(Decoder, DecodesStereoWavExactly) {
  const auto pcm = test::sineInt16(440.0, 44100, 4410, 2);
  const std::string path = test::tempPath("stereo.wav");
  ASSERT_TRUE(test::writeWav16(path, pcm, 44100, 2));

  DecodedAudio a;
  ASSERT_EQ(decodeFile(path, a), TC_OK);
  EXPECT_EQ(a.sampleRate, 44100u);
  EXPECT_EQ(a.channels, 2u);
  ASSERT_EQ(a.frames, 4410u);
  EXPECT_EQ(a.samples, pcm);

  DecodedAudio b;
  ASSERT_EQ(decodeFileDrLibs(path, b), TC_OK);
  EXPECT_EQ(b.samples, pcm);
  std::remove(path.c_str());
}

TEST(Decoder, DecodesMonoWav) {
  const auto pcm = test::sineInt16(1000.0, 48000, 480, 1);
  const std::string path = test::tempPath("mono.wav");
  ASSERT_TRUE(test::writeWav16(path, pcm, 48000, 1));
  DecodedAudio a;
  ASSERT_EQ(decodeFile(path, a), TC_OK);
  EXPECT_EQ(a.channels, 1u);
  EXPECT_EQ(a.samples, pcm);
  std::remove(path.c_str());
}

TEST(Track, MonoIsDuplicatedToStereo) {
  DecodedAudio a;
  a.sampleRate = 48000;
  a.channels = 1;
  a.samples = {100, -200, 300};
  a.frames = 3;
  auto t = prepareTrack(std::move(a), 48000);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->frames, 3u);
  EXPECT_EQ(t->pcm, (std::vector<int16_t>{100, 100, -200, -200, 300, 300}));
}

TEST(Track, ResamplesTo48k) {
  DecodedAudio a;
  a.sampleRate = 44100;
  a.channels = 2;
  a.samples = test::sineInt16(440.0, 44100, 44100, 2);
  a.frames = 44100;
  auto t = prepareTrack(std::move(a), 48000);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->frames, 48000u);
  EXPECT_EQ(t->pcm.size(), 96000u);
}

TEST(Track, EmptyInputGivesNull) {
  EXPECT_EQ(prepareTrack(DecodedAudio{}, 48000), nullptr);
}
