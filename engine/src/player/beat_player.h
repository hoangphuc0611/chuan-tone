#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/smoothed_param.h"
#include "core/spsc_ring.h"
#include "player/track.h"
#include "player/transposed_track.h"

namespace tonecore {

// Phát beat từ Track trong RAM, có thể đổi tone (bản TransposedTrack render dần ở luồng nền).
//
// Đồng bộ giữa luồng nền và audio thread không dùng khóa:
//  - Lệnh (beat mới / đổi tone): luồng nền đẩy con trỏ thô vào hàng đợi `incoming_`; audio thread
//    nhận ở đầu block kế tiếp. Con trỏ không còn dùng được audio thread trả về `retired_`; luồng
//    nền (giữ quyền sở hữu bằng shared_ptr) mới huỷ → audio thread không bao giờ cấp phát hay giải
//    phóng. Chỉ MỘT luồng (engine dùng Worker) được gọi setTrack / setTransposed / collectGarbage.
//  - Phát/tạm dừng: cờ atomic; âm lượng chuyển dần 5 ms để không có tiếng "bụp".
//  - Tua: yêu cầu atomic, audio thread áp dụng ở block kế tiếp.
//  - Đổi tone: audio thread tiếp tục phát bản đang nghe cho tới khi khối tại vị trí phát của bản
//  mới
//    đã render (TransposedTrack::blockReady), rồi chuyển bằng crossfade công suất đều 20 ms. Về
//    tone 0 = về beat gốc (luôn sẵn) → chuyển ngay.
//  - Vị trí phát rơi vào khối chưa render (vừa tua tới vùng mới): phát im lặng, vị trí đứng yên
//    (như đang tải) tới khi khối xong, rồi fade vào 5 ms.
class BeatPlayer {
 public:
  explicit BeatPlayer(uint32_t sampleRate);
  ~BeatPlayer();  // chỉ gọi khi audio thread đã dừng

  BeatPlayer(const BeatPlayer&) = delete;
  BeatPlayer& operator=(const BeatPlayer&) = delete;

  // ---- Luồng nền (một luồng duy nhất) ----
  // Thay beat (vị trí về 0, tạm dừng, tone 0). Trả false nếu hàng đợi đầy (rất hiếm).
  bool setTrack(std::shared_ptr<const Track> track);
  // Đổi tone: phát `track` (đang render) khi sẵn; nullptr = về beat gốc.
  bool setTransposed(std::shared_ptr<TransposedTrack> track);
  // Huỷ các track audio thread đã trả về.
  void collectGarbage();
  bool hasGarbage() const noexcept { return retired_.sizeApprox() > 0; }

  // ---- Luồng bất kỳ ----
  void play() noexcept;
  void pause() noexcept;
  void seekFrames(uint64_t frame) noexcept;
  bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
  uint64_t positionFrames() const noexcept { return position_.load(std::memory_order_relaxed); }
  uint64_t durationFrames() const noexcept { return duration_.load(std::memory_order_relaxed); }
  bool hasTrack() const noexcept { return duration_.load(std::memory_order_relaxed) > 0; }
  // true một lần sau khi phát hết bài (đọc xong thì xoá cờ).
  bool consumeEnded() noexcept { return ended_.exchange(false, std::memory_order_acq_rel); }
  // Số nửa cung của bản đang NGHE (đổi lúc bắt đầu crossfade sang bản mới).
  int audibleSemitones() const noexcept { return audible_.load(std::memory_order_relaxed); }
  // true khi đang chờ khối chưa render tại vị trí phát.
  bool starved() const noexcept { return starved_.load(std::memory_order_relaxed); }
  // (M6) Beat có stem giọng: phát phần nhạc + k · giọng; k 0…1 trượt 20 ms (0 = karaoke).
  void setVocalGain(float k) noexcept { vocalGain_.setTarget(k); }
  float vocalGain() const noexcept { return vocalGain_.target(); }
  bool hasVocal() const noexcept { return hasVocal_.load(std::memory_order_relaxed); }

  // ---- Audio thread ----
  // Ghi beat (stereo interleaved, float −1..1) vào `outStereo`, `frames` frame. [RT]
  void render(float* outStereo, uint32_t frames) noexcept;

 private:
  struct Command {
    const Track* track;   // != nullptr: beat mới
    TransposedTrack* tt;  // bản đổi tone (nullptr = beat gốc)
  };
  struct Retired {
    const Track* track;
    TransposedTrack* tt;
  };

  void adoptCommands() noexcept;
  void retire(const Track* track, TransposedTrack* tt) noexcept;
  bool available(const TransposedTrack* src, uint64_t frame) const noexcept;
  // Phần nhạc (+ g · giọng nếu có stem giọng) tại `frame`.
  void sampleAt(const TransposedTrack* src, uint64_t frame, float g, float& l,
                float& r) const noexcept;

  SpscRing<Command> incoming_{16};
  SpscRing<Retired> retired_{64};

  // Luồng nền: giữ quyền sở hữu những gì audio thread đang (hoặc có thể đang) dùng.
  std::vector<std::shared_ptr<const Track>> liveTracks_;
  std::vector<std::shared_ptr<TransposedTrack>> liveTransposed_;

  // Audio thread (và destructor).
  const Track* track_ = nullptr;
  TransposedTrack* active_ = nullptr;  // bản đang phát; nullptr = beat gốc
  TransposedTrack* next_ = nullptr;    // bản chờ chuyển sang (hasNext_)
  bool hasNext_ = false;
  TransposedTrack* fadeFrom_ = nullptr;  // bản cũ đang crossfade ra (fadeFromValid_)
  bool fadeFromValid_ = false;
  uint32_t xfade_ = 0;  // số frame crossfade còn lại (0 = không crossfade)
  uint32_t xfadeLen_;
  Retired deferred_[8]{};  // trả về chưa được (hàng đợi đầy) → thử lại block sau
  uint32_t deferredCount_ = 0;
  uint64_t cursor_ = 0;
  // Fade phát/dừng tuyến tính đúng 5 ms (đường mũ tiến về 0 quá lâu → vị trí vẫn trôi sau khi
  // dừng). Chỉ audio thread dùng.
  float fade_ = 0.0f;
  uint32_t fadeFrames_;
  float fadeStep_ = 1.0f;

  std::atomic<bool> playing_{false};
  std::atomic<int64_t> seekRequest_{-1};
  std::atomic<uint64_t> position_{0};
  std::atomic<uint64_t> duration_{0};
  std::atomic<bool> ended_{false};
  std::atomic<int> audible_{0};
  std::atomic<bool> starved_{false};
  std::atomic<bool> hasVocal_{false};
  SmoothedParam vocalGain_{0.0f, 0.0f, 1.0f};
};

}  // namespace tonecore
