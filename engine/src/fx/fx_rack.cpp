#include "fx/fx_rack.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

FxRack::FxRack(double sampleRate, uint32_t maxBlockFrames)
    : fs_(sampleRate),
      maxBlock_(maxBlockFrames),
      xfadeLen_(std::max<uint32_t>(1, static_cast<uint32_t>(kCrossfadeSec * sampleRate))),
      maxSpill_(static_cast<uint32_t>(kMaxSpillSec * sampleRate)) {
  wet_.prepare(sampleRate, 0.020);
  auto chain = std::make_unique<FxChain>(fs_, maxBlock_, FxPreset{}, 0);
  active_ = chain.get();
  liveChains_.push_back(std::move(chain));
}

// Audio thread đã dừng: mọi chuỗi / bản cập nhật thuộc liveChains_ / liveUpdates_, tự huỷ.
FxRack::~FxRack() = default;

FxPreset FxRack::effectiveLocked() const {
  FxPreset p = enabled_ ? desired_ : FxPreset{};
  p.delay.bpm = bpm_;
  return p;
}

void FxRack::applyLocked(const FxPreset& p) {
  if (needsRebuild(built_, p)) {
    auto chain = std::make_unique<FxChain>(fs_, maxBlock_, p, ++generation_);
    FxChain* raw = chain.get();
    liveChains_.push_back(std::move(chain));
    if (FxChain* stale = pending_.exchange(raw, std::memory_order_acq_rel)) {
      // Chưa từng tới audio thread (bị thay trước khi được nhận) → huỷ ngay.
      liveChains_.erase(std::remove_if(liveChains_.begin(), liveChains_.end(),
                                       [&](const auto& c) { return c.get() == stale; }),
                        liveChains_.end());
    }
  } else {
    auto u = std::make_unique<Update>(Update{p, ++generation_});
    Update* raw = u.get();
    liveUpdates_.push_back(std::move(u));
    if (Update* stale = update_.exchange(raw, std::memory_order_acq_rel)) {
      liveUpdates_.erase(std::remove_if(liveUpdates_.begin(), liveUpdates_.end(),
                                        [&](const auto& x) { return x.get() == stale; }),
                         liveUpdates_.end());
    }
  }
  built_ = p;
}

void FxRack::apply(const FxPreset& p) {
  std::lock_guard<std::mutex> lock(mutex_);
  collectGarbageLocked();
  desired_ = p;
  applyLocked(effectiveLocked());
}

FxPreset FxRack::preset() const {
  std::lock_guard<std::mutex> lock(mutex_);
  FxPreset p = desired_;
  p.delay.bpm = bpm_;
  return p;
}

void FxRack::setEnabled(bool on) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enabled_ == on) return;
  enabled_ = on;
  applyLocked(effectiveLocked());
}

bool FxRack::enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

void FxRack::setTempo(float bpm) {
  std::lock_guard<std::mutex> lock(mutex_);
  bpm_ = bpm > 0.0f ? std::clamp(bpm, 30.0f, 300.0f) : 100.0f;
  applyLocked(effectiveLocked());
}

float FxRack::tempo() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return bpm_;
}

void FxRack::collectGarbage() {
  std::lock_guard<std::mutex> lock(mutex_);
  collectGarbageLocked();
}

void FxRack::collectGarbageLocked() {
  Retired r{};
  while (retired_.pop(r)) {
    if (r.chain != nullptr) {
      liveChains_.erase(std::remove_if(liveChains_.begin(), liveChains_.end(),
                                       [&](const auto& c) { return c.get() == r.chain; }),
                        liveChains_.end());
    }
    if (r.update != nullptr) {
      liveUpdates_.erase(std::remove_if(liveUpdates_.begin(), liveUpdates_.end(),
                                        [&](const auto& u) { return u.get() == r.update; }),
                         liveUpdates_.end());
    }
  }
}

// [RT]
void FxRack::retire(FxChain* c, Update* u) noexcept {
  if (c == nullptr && u == nullptr) return;
  if (retired_.push(Retired{c, u})) return;
  // Hàng đợi trả về đầy (luồng điều khiển chưa dọn): giữ lại thử ở khối sau. Hết chỗ nữa thì để
  // nguyên — luồng điều khiển vẫn sở hữu (chỉ chậm giải phóng). Không xảy ra khi dùng bình thường.
  if (deferredCount_ < deferred_.size()) deferred_[deferredCount_++] = Retired{c, u};
}

// [RT]
void FxRack::startFadeOut(OldChain& o) noexcept {
  if (o.fading) return;
  o.fading = true;
  o.fadeOutLeft = xfadeLen_;
}

// [RT]
void FxRack::process(const float* voice, float* outL, float* outR, uint32_t n) noexcept {
  // Trả lại những gì lần trước chưa trả được.
  uint32_t kept = 0;
  for (uint32_t i = 0; i < deferredCount_; ++i) {
    if (!retired_.push(deferred_[i])) deferred_[kept++] = deferred_[i];
  }
  deferredCount_ = kept;

  // 1. Chuỗi mới: chuỗi đang dùng chuyển sang danh sách cũ, phần khô của nó giảm từ trọng số hiện
  //    tại (đang vào dở thì chưa tới 1) về 0.
  if (FxChain* c = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
    OldChain* slot = nullptr;
    for (auto& o : old_) {
      if (o.chain == nullptr) {
        slot = &o;
        break;
      }
    }
    if (slot == nullptr) {
      // Hết chỗ (đổi preset liên tục nhanh hơn 15 ms): bỏ chuỗi cũ nhất đang tắt dần.
      OldChain* victim = &old_[0];
      for (auto& o : old_) {
        if (o.fading && (!victim->fading || o.fadeOutLeft < victim->fadeOutLeft)) victim = &o;
      }
      retire(victim->chain, nullptr);
      *victim = OldChain{};
      slot = victim;
    }
    *slot = OldChain{active_, xfadeLen_ - activeXfadeLeft_, 0, 0, false};
    active_ = c;
    activeXfadeLeft_ = xfadeLen_;
    // Giới hạn số chuỗi cùng ngân: quá thì chuỗi ngân lâu nhất tắt dần.
    int spilling = 0;
    for (const auto& o : old_) spilling += o.chain != nullptr && !o.fading;
    while (spilling > kMaxSpilling) {
      OldChain* oldest = nullptr;
      for (auto& o : old_) {
        if (o.chain != nullptr && !o.fading && &o != slot &&
            (oldest == nullptr || o.spilled > oldest->spilled)) {
          oldest = &o;
        }
      }
      if (oldest == nullptr) break;
      startFadeOut(*oldest);
      --spilling;
    }
  }

  // 2. Thông số mới cho chuỗi đang dùng (bản cập nhật cũ hơn chuỗi thì bỏ).
  if (Update* u = update_.exchange(nullptr, std::memory_order_acq_rel)) {
    if (u->generation > active_->generation()) active_->applyContinuous(u->preset);
    retire(nullptr, u);
  }

  // 3. Xử lý: chuỗi đang dùng nhận giọng trọn; chuỗi cũ còn crossfade thì vẫn nhận giọng (lượng gửi
  //    giảm dần), xong thì chỉ còn đuôi.
  const auto len = static_cast<float>(xfadeLen_);
  active_->process(voice, n, true, 1.0f, 1.0f);
  for (auto& o : old_) {
    if (o.chain == nullptr) continue;
    if (o.xfadeLeft > 0) {
      const float s0 = static_cast<float>(o.xfadeLeft) / len;
      const float s1 =
          static_cast<float>(o.xfadeLeft > n ? o.xfadeLeft - n : 0) / static_cast<float>(xfadeLen_);
      o.chain->process(voice, n, true, s0, s1);
    } else {
      o.chain->process(nullptr, n, false, 0.0f, 0.0f);
    }
  }

  // 4. Trộn: khô theo trọng số crossfade, ướt × "Vang" (chuỗi bị ép tắt thì × hệ số tắt dần).
  const float* d = active_->dry();
  const float* wl = active_->wetL();
  const float* wr = active_->wetR();
  for (uint32_t i = 0; i < n; ++i) {
    float dry = d[i];
    if (activeXfadeLeft_ > i) {
      dry *= 1.0f - static_cast<float>(activeXfadeLeft_ - i - 1) / len;
    }
    outL[i] = dry;
    outR[i] = dry;
  }
  const bool wetActive = active_->hasWet();
  for (uint32_t i = 0; i < n; ++i) {
    const float g = wet_.next();
    if (wetActive) {
      outL[i] += g * wl[i];
      outR[i] += g * wr[i];
    }
  }
  for (auto& o : old_) {
    if (o.chain == nullptr) continue;
    const float* od = o.chain->dry();
    const float* ol = o.chain->wetL();
    const float* orr = o.chain->wetR();
    const float wetGain = wet_.current();
    for (uint32_t i = 0; i < n; ++i) {
      float dry = 0.0f;
      if (o.xfadeLeft > i + 1) dry = od[i] * static_cast<float>(o.xfadeLeft - i - 1) / len;
      float kill = 1.0f;
      if (o.fading) {
        kill = o.fadeOutLeft > i + 1 ? static_cast<float>(o.fadeOutLeft - i - 1) / len : 0.0f;
      }
      outL[i] += dry + kill * wetGain * ol[i];
      outR[i] += dry + kill * wetGain * orr[i];
    }
  }

  // 5. Cập nhật đếm; trả về chuỗi cũ đã tắt hẳn.
  activeXfadeLeft_ = activeXfadeLeft_ > n ? activeXfadeLeft_ - n : 0;
  int inUse = 1;
  for (auto& o : old_) {
    if (o.chain == nullptr) continue;
    o.xfadeLeft = o.xfadeLeft > n ? o.xfadeLeft - n : 0;
    bool done = false;
    if (o.fading) {
      o.fadeOutLeft = o.fadeOutLeft > n ? o.fadeOutLeft - n : 0;
      done = o.fadeOutLeft == 0;
    } else if (o.xfadeLeft == 0) {
      o.spilled += n;
      const bool silent = !o.chain->hasWet() || (o.spilled >= static_cast<uint32_t>(0.1 * fs_) &&
                                                 o.chain->wetMeanSquare() < kTailSilent);
      if (silent) {
        done = true;
      } else if (o.spilled > maxSpill_) {
        startFadeOut(o);
      }
    }
    if (done) {
      retire(o.chain, nullptr);
      o = OldChain{};
    } else {
      ++inUse;
    }
  }
  chainsInUse_.store(inUse, std::memory_order_relaxed);
}

}  // namespace tonecore
