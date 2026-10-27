#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "player/track.h"

namespace tonecore {

// Bản beat đã đổi tone, được luồng nền render dần từng khối (BeatRenderer) trong khi audio thread
// đang phát (BeatPlayer). Cùng độ dài, cùng vị trí từng mẫu với beat gốc.
//
// Đồng bộ không khoá: luồng render ghi xong PCM của khối b rồi mới ready[b].store(1, release);
// audio thread chỉ đọc khối b sau khi ready[b].load(acquire) == 1. Khối đã công bố không bao giờ bị
// ghi lại.
//
// PCM lưu int16 với dư 6 dB (mẫu thật = giá trị × 2 / 32768): đổi pha làm đỉnh có thể vượt 0 dBFS
// khi beat gốc đã sát 0 dBFS — lưu dư để không méo ngay trong bản lưu; phần vượt (nếu có) được xử
// lý ở tầng ra (kẹp, limiter từ M5) như với beat gốc. Nhiễu lượng tử vẫn ở ~−90 dBFS.
struct TransposedTrack {
  static constexpr uint32_t kBlockFrames = 4096;  // ~85 ms ở 48 kHz
  static constexpr float kStoreGain = 0.5f;       // dư 6 dB khi lưu int16

  TransposedTrack(std::shared_ptr<const Track> src, int semis)
      : source(std::move(src)),
        semitones(semis),
        frames(source->frames),
        blocks(static_cast<uint32_t>((frames + kBlockFrames - 1) / kBlockFrames)),
        // Không cần xoá về 0: chỉ đọc khối đã công bố.
        pcm(new int16_t[static_cast<std::size_t>(frames) * 2]),
        ready(new std::atomic<uint8_t>[blocks]()) {}

  bool blockReady(uint64_t frame) const noexcept {
    const auto b = static_cast<uint32_t>(frame / kBlockFrames);
    return b < blocks && ready[b].load(std::memory_order_acquire) != 0;
  }
  float progress() const noexcept {
    return blocks == 0 ? 1.0f
                       : static_cast<float>(readyCount.load(std::memory_order_relaxed)) /
                             static_cast<float>(blocks);
  }

  const std::shared_ptr<const Track> source;  // giữ beat gốc sống tới khi render xong
  const int semitones;
  const uint64_t frames;
  const uint32_t blocks;
  std::unique_ptr<int16_t[]> pcm;  // stereo interleaved, × kStoreGain
  std::unique_ptr<std::atomic<uint8_t>[]> ready;
  std::atomic<uint32_t> readyCount{0};
  std::atomic<int64_t> wantFrame{-1};  // tua tới vùng chưa có → luồng render ưu tiên chỗ này
  std::atomic<bool> cancelled{false};  // có yêu cầu tone khác → bỏ phần còn lại
};

}  // namespace tonecore
