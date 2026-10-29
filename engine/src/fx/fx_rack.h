#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "core/smoothed_param.h"
#include "core/spsc_ring.h"
#include "fx/fx_chain.h"

namespace tonecore {

// Giá đỡ chuỗi FX giọng và đổi preset không click (docs/m5-plan.md điểm 1–2, ADR-005).
//
// Luồng điều khiển (mọi hàm điều khiển giữ mutex riêng — không bao giờ chạm từ audio thread):
//  - apply(p): khác cấu trúc so với preset đang dùng (needsRebuild) → dựng FxChain mới (cấp phát
//    ở đây) và đặt vào ô `pending_` bằng atomic exchange; nếu ô còn chuỗi cũ chưa được nhận (bấm
//    liên tục) thì chuỗi đó chưa từng tới audio thread → huỷ ngay. Cùng cấu trúc → chỉ gửi bản
//    thông số mới qua ô `update_` (cũng exchange, chỉ giữ bản mới nhất).
//  - collectGarbage(): huỷ các chuỗi / bản cập nhật audio thread đã trả về (hàng đợi SPSC).
// Audio thread (process):
//  1. Nhận chuỗi mới (nếu có): chuỗi đang dùng thành "chuỗi cũ" — phần khô crossfade tuyến tính
//     15 ms sang chuỗi mới (cùng tiếng vào, xử lý gần giống → tương quan cao → cộng tuyến tính
//     không hụt), lượng gửi sang delay / reverb cũ giảm 1 → 0 cùng lúc; sau đó chuỗi cũ chỉ còn
//     xử lý đuôi ướt với tiếng vào = 0 ("ngân nốt").
//  2. Nhận bản cập nhật (nếu thế hệ mới hơn chuỗi đang dùng) → applyContinuous, thông số trượt dần.
//  3. Chuỗi cũ: đuôi < −80 dB → trả về ngay; ngân quá 4 s → tắt dần 15 ms rồi trả về. Quá 2 chuỗi
//     đang ngân → chuỗi cũ nhất tắt dần 15 ms.
//  4. Ra stereo = Σ khô · trọng số crossfade + "Vang" · Σ ướt.
// Preset "Khô" (mọi khối tắt): ra = vào đúng từng mẫu.
class FxRack {
 public:
  static constexpr double kCrossfadeSec = 0.015;
  static constexpr double kMaxSpillSec = 4.0;
  static constexpr float kTailSilent = 1e-8f;  // trung bình bình phương ≈ −80 dBFS
  static constexpr int kMaxSpilling = 2;

  FxRack(double sampleRate, uint32_t maxBlockFrames);
  ~FxRack();

  FxRack(const FxRack&) = delete;
  FxRack& operator=(const FxRack&) = delete;

  // ---- Luồng điều khiển ----
  void apply(const FxPreset& p);
  FxPreset preset() const;
  // Tắt = dùng chuỗi "Khô" (đuôi vang của preset đang dùng vẫn ngân nốt); bật lại = preset đã đặt.
  void setEnabled(bool on);
  bool enabled() const;
  // BPM cho delay theo nhịp (0 hoặc âm = không rõ → 100). Đổi thời gian trễ → dựng chuỗi mới.
  void setTempo(float bpm);
  float tempo() const;
  // "Vang": nhân phần ướt (0…2), trượt 20 ms.
  void setWet(float scale) noexcept { wet_.setTarget(scale); }
  float wet() const noexcept { return wet_.target(); }
  void collectGarbage();

  // Số chuỗi audio thread đang chạy (1 + số chuỗi cũ còn ngân) — đo / test.
  int audioChains() const noexcept { return chainsInUse_.load(std::memory_order_relaxed); }

  // ---- Audio thread ----
  // voice: mono (n ≤ maxBlockFrames); outL / outR: ghi đè. [RT]
  void process(const float* voice, float* outL, float* outR, uint32_t n) noexcept;

 private:
  struct Update {
    FxPreset preset;
    uint64_t generation;
  };
  struct Retired {
    FxChain* chain;
    Update* update;
  };
  struct OldChain {
    FxChain* chain = nullptr;
    uint32_t xfadeLeft = 0;    // mẫu crossfade phần khô còn lại (0 = chỉ còn đuôi ướt)
    uint32_t spilled = 0;      // số mẫu đã ngân
    uint32_t fadeOutLeft = 0;  // > 0: đang tắt dần (bị ép)
    bool fading = false;
  };
  static constexpr int kOldSlots = 6;

  // Luồng điều khiển (giữ mutex_).
  void applyLocked(const FxPreset& p);
  void collectGarbageLocked();
  FxPreset effectiveLocked() const;
  // Audio thread.
  void retire(FxChain* c, Update* u) noexcept;
  void startFadeOut(OldChain& o) noexcept;

  const double fs_;
  const uint32_t maxBlock_;
  const uint32_t xfadeLen_;
  const uint32_t maxSpill_;

  mutable std::mutex mutex_;
  FxPreset desired_;  // preset người dùng chọn (kể cả khi đang tắt FX)
  FxPreset built_;    // cấu trúc của chuỗi gần nhất đã dựng
  bool enabled_ = true;
  float bpm_ = 100.0f;
  uint64_t generation_ = 0;
  std::vector<std::unique_ptr<FxChain>> liveChains_;
  std::vector<std::unique_ptr<Update>> liveUpdates_;

  std::atomic<FxChain*> pending_{nullptr};
  std::atomic<Update*> update_{nullptr};
  SpscRing<Retired> retired_{32};
  std::atomic<int> chainsInUse_{1};
  SmoothedParam wet_{1.0f, 0.0f, 2.0f};

  // Chỉ audio thread.
  FxChain* active_ = nullptr;
  uint32_t activeXfadeLeft_ = 0;  // phần khô của chuỗi mới đang vào (0 = trọn)
  std::array<OldChain, kOldSlots> old_{};
  std::array<Retired, 8> deferred_{};  // trả về chưa được (hàng đợi đầy) → thử lại khối sau
  uint32_t deferredCount_ = 0;
};

}  // namespace tonecore
