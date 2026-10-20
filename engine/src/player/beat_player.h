#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/spsc_ring.h"
#include "player/track.h"

namespace tonecore {

// Phát beat từ Track trong RAM.
//
// Đồng bộ giữa luồng điều khiển và audio thread không dùng khóa:
//  - Track mới: luồng điều khiển đẩy con trỏ vào hàng đợi `incoming_`; audio thread nhận ở đầu
//    block kế tiếp, đẩy track cũ vào `retired_`. Luồng điều khiển huỷ track cũ khi dọn dẹp →
//    audio thread không bao giờ cấp phát hay giải phóng bộ nhớ.
//  - Phát/tạm dừng: cờ atomic; âm lượng chuyển dần 5 ms để không có tiếng "bụp".
//  - Tua: yêu cầu atomic, audio thread áp dụng ở block kế tiếp.
class BeatPlayer {
 public:
  explicit BeatPlayer(uint32_t sampleRate);
  ~BeatPlayer();  // chỉ gọi khi audio thread đã dừng

  BeatPlayer(const BeatPlayer&) = delete;
  BeatPlayer& operator=(const BeatPlayer&) = delete;

  // ---- Luồng điều khiển ----
  // Thay track (vị trí về 0, trạng thái tạm dừng). Trả false nếu hàng đợi đầy (rất hiếm).
  bool setTrack(std::unique_ptr<Track> track);
  void play() noexcept;
  void pause() noexcept;
  void seekFrames(uint64_t frame) noexcept;
  bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
  uint64_t positionFrames() const noexcept { return position_.load(std::memory_order_relaxed); }
  uint64_t durationFrames() const noexcept { return duration_.load(std::memory_order_relaxed); }
  bool hasTrack() const noexcept { return duration_.load(std::memory_order_relaxed) > 0; }
  // true một lần sau khi phát hết bài (đọc xong thì xoá cờ).
  bool consumeEnded() noexcept { return ended_.exchange(false, std::memory_order_acq_rel); }
  // Huỷ các track cũ mà audio thread đã trả về. Chỉ MỘT luồng được gọi hàm này và setTrack()
  // (engine dùng luồng nền) vì `retired_` là hàng đợi một bên đọc.
  void collectGarbage();
  // Ước lượng có track cũ đang chờ huỷ (gọi từ luồng bất kỳ).
  bool hasGarbage() const noexcept { return retired_.sizeApprox() > 0; }

  // ---- Audio thread ----
  // Ghi beat (stereo interleaved, float −1..1) vào `outStereo`, `frames` frame. [RT]
  void render(float* outStereo, uint32_t frames) noexcept;

 private:
  void adoptIncoming() noexcept;

  SpscRing<Track*> incoming_{8};
  SpscRing<Track*> retired_{16};  // > incoming_ → không bao giờ đầy giữa 2 lần dọn
  Track* current_ = nullptr;      // chỉ audio thread (và destructor)
  uint64_t cursor_ = 0;           // chỉ audio thread
  // Fade phát/dừng tuyến tính đúng 5 ms (đường mũ tiến về 0 quá lâu → vị trí vẫn trôi sau
  // khi dừng). Chỉ audio thread dùng.
  float fade_ = 0.0f;
  float fadeStep_ = 1.0f;

  std::atomic<bool> playing_{false};
  std::atomic<int64_t> seekRequest_{-1};
  std::atomic<uint64_t> position_{0};
  std::atomic<uint64_t> duration_{0};
  std::atomic<bool> ended_{false};
};

}  // namespace tonecore
