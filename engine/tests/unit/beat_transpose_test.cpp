// Đổi tone beat trong engine (M4 B5): BeatPlayer chuyển bản / chờ khối, BeatRenderer render nền.
// Các test ở đây tất định: tự công bố khối (BeatPlayer) hoặc chờ Worker xong (BeatRenderer).

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "core/worker.h"
#include "player/beat_player.h"
#include "player/beat_renderer.h"

using namespace tonecore;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kB = TransposedTrack::kBlockFrames;
constexpr double kPi = 3.14159265358979323846;

std::shared_ptr<Track> sineTrack(double hz, double seconds, double amp = 0.25) {
  auto t = std::make_shared<Track>();
  t->frames = static_cast<uint64_t>(seconds * kRate);
  t->pcm.resize(2 * t->frames);
  for (uint64_t i = 0; i < t->frames; ++i) {
    const auto v = static_cast<int16_t>(
        std::lround(amp * 32767.0 * std::sin(2 * kPi * hz * static_cast<double>(i) / kRate)));
    t->pcm[2 * i] = v;
    t->pcm[2 * i + 1] = v;
  }
  return t;
}

// Điền khối b của bản đổi tone bằng hằng số `value` (để nhận ra bản nào đang phát) rồi công bố.
void publish(TransposedTrack& tt, uint32_t b, float value) {
  const uint64_t start = static_cast<uint64_t>(b) * kB;
  const uint64_t end = std::min<uint64_t>(tt.frames, start + kB);
  const auto v = static_cast<int16_t>(std::lround(value * TransposedTrack::kStoreGain * 32767.0));
  for (uint64_t i = start; i < end; ++i) {
    tt.pcm[2 * i] = v;
    tt.pcm[2 * i + 1] = v;
  }
  tt.ready[b].store(1, std::memory_order_release);
}

std::vector<float> renderFrames(BeatPlayer& p, uint32_t frames) {
  std::vector<float> out(2 * static_cast<std::size_t>(frames));
  for (uint32_t done = 0; done < frames; done += 256) {
    p.render(out.data() + 2 * static_cast<std::size_t>(done), std::min(256u, frames - done));
  }
  return out;
}

}  // namespace

// Bản mới chưa có khối tại vị trí phát → vẫn phát bản cũ; có rồi → crossfade 20 ms sang bản mới,
// tone đang nghe đổi đúng lúc đó.
TEST(BeatPlayer, SwitchesOnlyWhenBlockReadyThenCrossfades) {
  BeatPlayer p(kRate);
  auto src = sineTrack(440.0, 2.0);
  ASSERT_TRUE(p.setTrack(src));
  p.play();
  renderFrames(p, 4800);  // fade vào xong
  auto tt = std::make_shared<TransposedTrack>(src, 3);
  ASSERT_TRUE(p.setTransposed(tt));
  const auto before = renderFrames(p, 2048);
  EXPECT_EQ(p.audibleSemitones(), 0);  // khối chưa có → vẫn beat gốc
  EXPECT_NE(before[2 * 2000], 0.5f);
  for (uint32_t b = 0; b < tt->blocks; ++b) publish(*tt, b, 0.5f);
  const auto after = renderFrames(p, 2400);
  EXPECT_EQ(p.audibleSemitones(), 3);
  // Sau 20 ms (960 frame) chỉ còn bản mới (hằng 0,5); trong 20 ms đó là crossfade, không nhảy.
  for (uint32_t i = 1000; i < 2400; ++i) EXPECT_NEAR(after[2 * i], 0.5f, 1e-4f) << i;
  float maxJump = 0.0f;
  for (uint32_t i = 1; i < 1000; ++i)
    maxJump = std::max(maxJump, std::abs(after[2 * i] - after[2 * (i - 1)]));
  EXPECT_LT(maxJump, 0.05f);
}

// Tua tới khối chưa render: im lặng, vị trí đứng yên; khối xong → phát tiếp từ đúng chỗ, fade vào.
TEST(BeatPlayer, SeekIntoUnrenderedBlockWaits) {
  BeatPlayer p(kRate);
  auto src = sineTrack(440.0, 2.0);
  ASSERT_TRUE(p.setTrack(src));
  auto tt = std::make_shared<TransposedTrack>(src, -2);
  publish(*tt, 0, 0.5f);
  ASSERT_TRUE(p.setTransposed(tt));
  p.play();
  renderFrames(p, 1024);
  ASSERT_EQ(p.audibleSemitones(), -2);
  const uint64_t target = 10ull * kB + 100;
  p.seekFrames(target);
  const auto waiting = renderFrames(p, 2048);
  for (float v : waiting) EXPECT_EQ(v, 0.0f);
  EXPECT_TRUE(p.starved());
  EXPECT_EQ(p.positionFrames(), target);
  publish(*tt, 10, 0.5f);
  const auto resumed = renderFrames(p, 1024);
  EXPECT_FALSE(p.starved());
  EXPECT_EQ(p.positionFrames(), target + 1024);
  EXPECT_LT(resumed[0], 0.5f);  // fade vào 5 ms
  EXPECT_NEAR(resumed[2 * 1000], 0.5f, 1e-4f);
}

// Về tone 0 = beat gốc (luôn sẵn): chuyển ngay, sau crossfade đúng từng mẫu với beat gốc.
TEST(BeatPlayer, BackToZeroIsBitExactOriginal) {
  BeatPlayer p(kRate);
  auto src = sineTrack(440.0, 2.0);
  ASSERT_TRUE(p.setTrack(src));
  auto tt = std::make_shared<TransposedTrack>(src, 5);
  for (uint32_t b = 0; b < tt->blocks; ++b) publish(*tt, b, 0.3f);
  ASSERT_TRUE(p.setTransposed(tt));
  p.play();
  renderFrames(p, 4800);
  ASSERT_EQ(p.audibleSemitones(), 5);
  ASSERT_TRUE(p.setTransposed(nullptr));
  const uint64_t pos = p.positionFrames();
  const auto out = renderFrames(p, 4800);
  EXPECT_EQ(p.audibleSemitones(), 0);
  for (uint32_t i = 1200; i < 4800; ++i) {
    ASSERT_EQ(out[2 * i], static_cast<float>(src->pcm[2 * (pos + i)]) / 32768.0f) << i;
  }
  p.collectGarbage();
}

// Lệnh tone mới đến khi bản trước còn chưa kịp nghe → bỏ bản trước, chỉ chuyển sang bản cuối.
TEST(BeatPlayer, LatestRequestWins) {
  BeatPlayer p(kRate);
  auto src = sineTrack(440.0, 2.0);
  ASSERT_TRUE(p.setTrack(src));
  p.play();
  renderFrames(p, 512);
  auto a = std::make_shared<TransposedTrack>(src, 1);
  auto b = std::make_shared<TransposedTrack>(src, 2);
  ASSERT_TRUE(p.setTransposed(a));
  renderFrames(p, 256);
  ASSERT_TRUE(p.setTransposed(b));
  for (uint32_t k = 0; k < a->blocks; ++k) publish(*a, k, 0.2f);
  renderFrames(p, 256);
  EXPECT_EQ(p.audibleSemitones(), 0);  // a đã bị thay trước khi kịp nghe
  for (uint32_t k = 0; k < b->blocks; ++k) publish(*b, k, 0.4f);
  renderFrames(p, 2048);
  EXPECT_EQ(p.audibleSemitones(), 2);
  // a được trả về và huỷ ở luồng nền (chỉ còn tham chiếu của test).
  p.collectGarbage();
  EXPECT_EQ(a.use_count(), 1);
}

// Render nền từ giữa bài: xong mọi khối, chỗ nối (lượt 2 chạy từ đầu bài tới chỗ lượt 1 bắt đầu)
// liền mạch — không có bước nhảy lớn hơn ở phần còn lại của bài.
TEST(BeatRenderer, RendersWholeTrackAndJoinsSmoothly) {
  auto src = sineTrack(330.0, 3.0);
  auto tt = std::make_shared<TransposedTrack>(src, 4);
  const uint64_t from = 17ull * kB + 1234;
  int doneSemis = 0;
  {
    Worker w;
    BeatRenderer::start(w, tt, from, kRate, nullptr, [&](int s, double) { doneSemis = s; });
    w.waitIdle();
  }
  EXPECT_EQ(doneSemis, 4);
  EXPECT_EQ(tt->readyCount.load(), tt->blocks);
  const auto at = [&](uint64_t i) { return static_cast<float>(tt->pcm[2 * i]) / 16384.0f; };
  float maxJump = 0.0f;
  for (uint64_t i = 2 * kB; i < tt->frames - kB; ++i)
    maxJump = std::max(maxJump, std::abs(at(i) - at(i - 1)));
  float joinJump = 0.0f;
  const uint64_t join = 17ull * kB;
  for (uint64_t i = join - BeatRenderer::kLeadInFrames - 64; i < join + 64; ++i) {
    joinJump = std::max(joinJump, std::abs(at(i) - at(i - 1)));
  }
  // Sin 330 Hz × 2^(4/12), biên độ 0,25: bước lớn nhất ≈ 2π·416/48000·0,25 ≈ 0,0136.
  EXPECT_LT(maxJump, 0.016f);
  EXPECT_LT(joinJump, 0.016f);
}

// Huỷ trước khi bắt đầu → không render khối nào; tua (wantFrame) → khối đó có ngay ở bước đầu.
TEST(BeatRenderer, CancelAndSeekPriority) {
  auto src = sineTrack(330.0, 3.0);
  {
    auto tt = std::make_shared<TransposedTrack>(src, 2);
    tt->cancelled.store(true);
    Worker w;
    BeatRenderer::start(w, tt, 0, kRate, nullptr, nullptr);
    w.waitIdle();
    EXPECT_EQ(tt->readyCount.load(), 0u);
  }
  {
    auto tt = std::make_shared<TransposedTrack>(src, 2);
    const uint64_t want = 30ull * kB + 10;
    tt->wantFrame.store(static_cast<int64_t>(want));
    bool firstStepHadWant = false;
    bool first = true;
    Worker w;
    BeatRenderer::start(
        w, tt, 0, kRate,
        [&](float) {
          if (first) firstStepHadWant = tt->blockReady(want);
          first = false;
        },
        nullptr);
    w.waitIdle();
    EXPECT_TRUE(firstStepHadWant);
    EXPECT_EQ(tt->readyCount.load(), tt->blocks);
  }
}

// Luồng render chậm hơn đầu phát (khối kế tiếp chưa có): fade ra trước khi hết khối — không tắt
// phụt (click) — rồi đứng yên chờ.
TEST(BeatPlayer, FadesOutBeforeMissingBlock) {
  BeatPlayer p(kRate);
  auto src = sineTrack(440.0, 2.0);
  ASSERT_TRUE(p.setTrack(src));
  auto tt = std::make_shared<TransposedTrack>(src, 1);
  publish(*tt, 0, 0.5f);
  ASSERT_TRUE(p.setTransposed(tt));
  p.play();
  const auto out = renderFrames(p, 2 * kB);
  float maxJump = 0.0f;
  for (uint32_t i = 1; i < 2 * kB; ++i)
    maxJump = std::max(maxJump, std::abs(out[2 * i] - out[2 * (i - 1)]));
  EXPECT_LT(maxJump, 0.01f);  // fade 5 ms của mức 0,5: mỗi mẫu ≤ 0,5/240
  EXPECT_TRUE(p.starved());
  EXPECT_LE(p.positionFrames(), static_cast<uint64_t>(kB));
  EXPECT_GE(p.positionFrames(), static_cast<uint64_t>(kB) - 300);
}

// Beat master sát 0 dBFS: bản render nền (qua limiter) không có mẫu nào vượt 0 dBFS — trước B4,
// đổi pha làm đỉnh lên tới +4 dB và tầng ra kẹp thành méo.
TEST(BeatRenderer, LimitsOversAfterPhaseChange) {
  // Tổng hoạ âm cùng pha, đỉnh 0,99 — đổi pha làm đỉnh tăng.
  auto src = std::make_shared<Track>();
  src->frames = 3 * kRate;
  src->pcm.resize(2 * src->frames);
  for (uint64_t i = 0; i < src->frames; ++i) {
    double s = 0.0;
    for (int h = 1; h <= 6; ++h)
      s += std::cos(2 * kPi * 110.0 * h * static_cast<double>(i) / kRate) / h;
    const auto v = static_cast<int16_t>(std::lround(0.99 * 32767.0 * s / 2.45));
    src->pcm[2 * i] = v;
    src->pcm[2 * i + 1] = v;
  }
  auto tt = std::make_shared<TransposedTrack>(src, 5);
  {
    Worker w;
    BeatRenderer::start(w, tt, 0, kRate, nullptr, nullptr);
    w.waitIdle();
  }
  ASSERT_EQ(tt->readyCount.load(), tt->blocks);
  float peak = 0.0f;
  for (uint64_t i = 0; i < 2 * tt->frames; ++i) {
    peak = std::max(peak, std::abs(static_cast<float>(tt->pcm[i]) / 16384.0f));
  }
  EXPECT_LE(peak, 0.99f);
  EXPECT_GT(peak, 0.9f);  // vẫn to như gốc (chỉ cắt đỉnh, không hạ cả bài)
}
