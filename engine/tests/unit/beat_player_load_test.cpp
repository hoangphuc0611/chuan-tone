// Đổi beat khi audio thread chưa nhận (M9 B1, vấn đề #11 trong docs/m8-usability.md): engine báo
// "nạp xong" ngay khi đưa lệnh cho BeatPlayer, nên hàm đọc phải trả trạng thái của beat MỚI ngay —
// vị trí 0 (hoặc chỗ vừa tua), tone 0, chưa hết bài — và block đang chạy dở của beat cũ không được
// ghi trạng thái của nó đè lên.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "player/beat_player.h"

using namespace tonecore;

namespace {

constexpr uint32_t kRate = 48000;

std::shared_ptr<Track> constTrack(uint64_t frames, int16_t value = 8000) {
  auto t = std::make_shared<Track>();
  t->frames = frames;
  t->pcm.assign(2 * frames, value);
  return t;
}

void render(BeatPlayer& p, uint32_t frames) {
  std::vector<float> out(2 * static_cast<std::size_t>(frames));
  p.render(out.data(), frames);
}

}  // namespace

TEST(BeatPlayerLoad, ReadsNewTrackStateBeforeAudioThreadAdopts) {
  BeatPlayer p(kRate);
  auto a = constTrack(10 * kRate);
  ASSERT_TRUE(p.setTrack(a));
  render(p, 256);
  // Bản +3 nửa cung, công bố hết các khối → nghe +3 ngay.
  auto tt = std::make_shared<TransposedTrack>(a, 3);
  std::fill(tt->pcm.get(), tt->pcm.get() + 2 * tt->frames, int16_t{0});
  for (uint32_t b = 0; b < tt->blocks; ++b) tt->ready[b].store(1);
  ASSERT_TRUE(p.setTransposed(tt));
  p.play();
  for (int i = 0; i < 20; ++i) render(p, 256);
  ASSERT_EQ(p.audibleSemitones(), 3);
  ASSERT_GT(p.positionFrames(), 4000u);

  ASSERT_TRUE(p.setTrack(constTrack(5 * kRate)));
  // Audio thread chưa chạy block nào từ lúc đổi beat.
  EXPECT_TRUE(p.pending());
  EXPECT_EQ(p.positionFrames(), 0u);
  EXPECT_EQ(p.audibleSemitones(), 0);
  EXPECT_FALSE(p.isPlaying());
  EXPECT_EQ(p.durationFrames(), 5u * kRate);

  render(p, 256);
  EXPECT_FALSE(p.pending());
  EXPECT_EQ(p.positionFrames(), 0u);
  EXPECT_EQ(p.audibleSemitones(), 0);
}

TEST(BeatPlayerLoad, OldTrackEndDoesNotLeakIntoNewTrack) {
  BeatPlayer p(kRate);
  ASSERT_TRUE(p.setTrack(constTrack(1000)));
  render(p, 64);
  p.play();
  for (int i = 0; i < 40; ++i) render(p, 64);  // hết bài; cờ "hết bài" còn chưa ai đọc
  ASSERT_FALSE(p.isPlaying());

  ASSERT_TRUE(p.setTrack(constTrack(5 * kRate)));
  EXPECT_FALSE(p.consumeEnded());
  render(p, 64);
  EXPECT_FALSE(p.consumeEnded());  // audio thread xoá cờ của beat cũ lúc nhận beat mới
}

TEST(BeatPlayerLoad, SeekBeforeAdoptIsKept) {
  BeatPlayer p(kRate);
  ASSERT_TRUE(p.setTrack(constTrack(5 * kRate)));
  render(p, 64);
  ASSERT_TRUE(p.setTrack(constTrack(8 * kRate)));
  p.seekFrames(12345);
  EXPECT_EQ(p.positionFrames(), 12345u);
  render(p, 64);
  EXPECT_EQ(p.positionFrames(), 12345u);
}

TEST(BeatPlayerLoad, UnloadReadsEmptyImmediately) {
  BeatPlayer p(kRate);
  ASSERT_TRUE(p.setTrack(constTrack(5 * kRate)));
  render(p, 64);
  p.play();
  for (int i = 0; i < 10; ++i) render(p, 64);
  ASSERT_TRUE(p.unload());
  EXPECT_FALSE(p.hasTrack());
  EXPECT_EQ(p.positionFrames(), 0u);
  render(p, 64);
  EXPECT_EQ(p.positionFrames(), 0u);
}

// Hai luồng như trên máy: audio thread chạy liên tục (hầu như lúc nào cũng đang ở giữa một block),
// luồng nền đổi beat lúc beat cũ đang phát. Ngay sau mỗi lần đổi (beat mới tạm dừng ở 0), mọi lần
// đọc vị trí phải là 0 — trước khi sửa, block dở của beat cũ ghi vị trí cũ đè lên.
TEST(BeatPlayerLoad, ConcurrentLoadsNeverShowOldPosition) {
  BeatPlayer p(kRate);
  std::shared_ptr<const Track> tracks[2] = {constTrack(60 * kRate, 6000),
                                            constTrack(60 * kRate, -6000)};
  std::atomic<bool> stop{false};
  std::thread audio([&] {
    std::vector<float> out(2 * 64);
    while (!stop.load(std::memory_order_relaxed)) p.render(out.data(), 64);
  });

  int stale = 0;
  int ended = 0;
  int waited = 0;
  for (int i = 0; i < 2000; ++i) {
    ASSERT_TRUE(p.setTrack(tracks[i % 2]));
    for (int k = 0; k < 200; ++k) {
      if (p.positionFrames() != 0) ++stale;
      if (p.consumeEnded()) ++ended;
    }
    // Cho beat mới phát một đoạn để lần đổi sau rơi vào lúc đang phát.
    p.play();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (p.positionFrames() < 2048 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    if (p.positionFrames() >= 2048) ++waited;
  }
  stop.store(true);
  audio.join();
  EXPECT_EQ(stale, 0);
  EXPECT_EQ(ended, 0);
  EXPECT_EQ(waited, 2000);  // beat mới luôn phát được sau khi đổi
}
