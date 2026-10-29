#include "player/beat_player.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {
constexpr double kFadeSec = 0.005;       // 5 ms: đủ ngắn để không trễ, đủ dài để không "bụp"
constexpr double kCrossfadeSec = 0.020;  // đổi tone: 20 ms (CLAUDE.md 3.2: 10–20 ms)
constexpr float kInt16ToFloat = 1.0f / 32768.0f;
constexpr float kTransposedToFloat = kInt16ToFloat / TransposedTrack::kStoreGain;
constexpr float kHalfPi = 1.57079632679489661923f;
}  // namespace

BeatPlayer::BeatPlayer(uint32_t sampleRate)
    : xfadeLen_(std::max<uint32_t>(1, static_cast<uint32_t>(kCrossfadeSec * sampleRate))),
      fadeFrames_(static_cast<uint32_t>(kFadeSec * sampleRate)),
      fadeStep_(static_cast<float>(1.0 / (kFadeSec * sampleRate))) {
  vocalGain_.prepare(sampleRate, 0.020);
}

// Audio thread đã dừng: mọi con trỏ thô đều thuộc liveTracks_ / liveTransposed_, tự huỷ theo.
BeatPlayer::~BeatPlayer() = default;

bool BeatPlayer::setTrack(std::shared_ptr<const Track> track) {
  collectGarbage();
  if (!track) return false;
  if (!incoming_.push(Command{track.get(), nullptr})) return false;
  const uint64_t frames = track->frames;
  liveTracks_.push_back(std::move(track));
  playing_.store(false, std::memory_order_relaxed);
  seekRequest_.store(-1, std::memory_order_relaxed);
  position_.store(0, std::memory_order_relaxed);
  hasVocal_.store(liveTracks_.back()->hasVocal(), std::memory_order_relaxed);
  duration_.store(frames, std::memory_order_relaxed);
  ended_.store(false, std::memory_order_relaxed);
  return true;
}

bool BeatPlayer::setTransposed(std::shared_ptr<TransposedTrack> track) {
  collectGarbage();
  if (!incoming_.push(Command{nullptr, track.get()})) return false;
  if (track) liveTransposed_.push_back(std::move(track));
  return true;
}

void BeatPlayer::play() noexcept {
  if (!hasTrack()) return;
  // Đã ở cuối bài → phát lại từ đầu.
  if (positionFrames() >= durationFrames()) seekFrames(0);
  playing_.store(true, std::memory_order_relaxed);
}

void BeatPlayer::pause() noexcept {
  playing_.store(false, std::memory_order_relaxed);
}

void BeatPlayer::seekFrames(uint64_t frame) noexcept {
  const uint64_t clamped = std::min(frame, durationFrames());
  seekRequest_.store(static_cast<int64_t>(clamped), std::memory_order_relaxed);
  position_.store(clamped, std::memory_order_relaxed);
}

void BeatPlayer::collectGarbage() {
  Retired r{};
  while (retired_.pop(r)) {
    if (r.track != nullptr) {
      liveTracks_.erase(std::remove_if(liveTracks_.begin(), liveTracks_.end(),
                                       [&](const auto& p) { return p.get() == r.track; }),
                        liveTracks_.end());
    }
    if (r.tt != nullptr) {
      liveTransposed_.erase(std::remove_if(liveTransposed_.begin(), liveTransposed_.end(),
                                           [&](const auto& p) { return p.get() == r.tt; }),
                            liveTransposed_.end());
    }
  }
}

// [RT]
void BeatPlayer::retire(const Track* track, TransposedTrack* tt) noexcept {
  if (track == nullptr && tt == nullptr) return;
  if (retired_.push(Retired{track, tt})) return;
  // Hàng đợi trả về đầy (luồng nền chưa dọn): giữ lại thử ở block sau. Hết chỗ nữa thì để nguyên
  // (luồng nền vẫn giữ shared_ptr → chỉ chậm giải phóng, không hỏng). Không xảy ra khi dùng bình
  // thường.
  if (deferredCount_ < std::size(deferred_)) deferred_[deferredCount_++] = Retired{track, tt};
}

// [RT]
void BeatPlayer::adoptCommands() noexcept {
  // Thử trả lại những gì lần trước chưa trả được.
  uint32_t kept = 0;
  for (uint32_t i = 0; i < deferredCount_; ++i) {
    if (!retired_.push(deferred_[i])) deferred_[kept++] = deferred_[i];
  }
  deferredCount_ = kept;

  Command cmd{};
  while (incoming_.pop(cmd)) {
    if (cmd.track != nullptr) {
      // Beat mới: trả lại mọi thứ của beat cũ, về đầu bài, tone 0.
      retire(track_, active_);
      if (hasNext_) retire(nullptr, next_);
      if (fadeFromValid_) retire(nullptr, fadeFrom_);
      track_ = cmd.track;
      active_ = nullptr;
      hasNext_ = false;
      fadeFromValid_ = false;
      xfade_ = 0;
      cursor_ = 0;
      fade_ = 0.0f;
      audible_.store(0, std::memory_order_relaxed);
      continue;
    }
    // Đổi tone: bản chờ cũ (chưa kịp nghe) bị thay luôn.
    if (hasNext_) retire(nullptr, next_);
    next_ = cmd.tt;
    hasNext_ = next_ != active_;
  }
}

// [RT]
bool BeatPlayer::available(const TransposedTrack* src, uint64_t frame) const noexcept {
  if (track_ == nullptr || frame >= track_->frames) return false;
  return src == nullptr || src->blockReady(frame);
}

// [RT]
void BeatPlayer::sampleAt(const TransposedTrack* src, uint64_t frame, float g, float& l,
                          float& r) const noexcept {
  const auto i = 2 * static_cast<std::size_t>(frame);
  const int16_t* music = src == nullptr ? track_->pcm.data() : src->pcm.get();
  const int16_t* vocal =
      src == nullptr ? (track_->hasVocal() ? track_->vocal.data() : nullptr) : src->vocal.get();
  const float scale = src == nullptr ? kInt16ToFloat : kTransposedToFloat;
  float a = static_cast<float>(music[i]);
  float b = static_cast<float>(music[i + 1]);
  if (vocal != nullptr && g != 0.0f) {
    a += g * static_cast<float>(vocal[i]);
    b += g * static_cast<float>(vocal[i + 1]);
  }
  l = a * scale;
  r = b * scale;
}

// [RT] Không cấp phát, không khóa.
void BeatPlayer::render(float* outStereo, uint32_t frames) noexcept {
  adoptCommands();

  const int64_t seek = seekRequest_.exchange(-1, std::memory_order_relaxed);
  if (seek >= 0 && track_ != nullptr) {
    cursor_ = std::min<uint64_t>(static_cast<uint64_t>(seek), track_->frames);
    if (xfade_ > 0) {  // tua giữa lúc crossfade: bỏ luôn bản cũ
      xfade_ = 0;
      retire(nullptr, fadeFrom_);
      fadeFromValid_ = false;
    }
  }

  // Bản mới đã có tại vị trí phát → chuyển (cả khi đang tạm dừng: key chỉnh giọng và tone hiển thị
  // đổi ngay). Bản cũ đang phát được thì crossfade, không thì (đang chờ khối / tạm dừng) chuyển
  // thẳng.
  if (hasNext_ && available(next_, std::min(cursor_, track_ != nullptr ? track_->frames - 1 : 0))) {
    if (fadeFromValid_) retire(nullptr, fadeFrom_);  // crossfade trước chưa xong: bỏ bản cũ nhất
    fadeFromValid_ = false;
    xfade_ = 0;
    if (available(active_, cursor_) && fade_ > 0.0f) {
      fadeFrom_ = active_;
      fadeFromValid_ = true;
      xfade_ = xfadeLen_;
    } else {
      retire(nullptr, active_);
    }
    active_ = next_;
    hasNext_ = false;
    audible_.store(active_ != nullptr ? active_->semitones : 0, std::memory_order_relaxed);
  }

  const bool wantPlay = playing_.load(std::memory_order_relaxed) && track_ != nullptr;
  if (track_ == nullptr || (!wantPlay && fade_ <= 0.0f)) {
    std::fill(outStereo, outStereo + 2 * static_cast<std::size_t>(frames), 0.0f);
    starved_.store(false, std::memory_order_relaxed);
    if (track_ != nullptr && seekRequest_.load(std::memory_order_relaxed) < 0) {
      position_.store(cursor_, std::memory_order_relaxed);
    }
    return;
  }

  const uint64_t total = track_->frames;
  const bool withVocal = track_->hasVocal();
  bool starved = false;
  for (uint32_t i = 0; i < frames; ++i) {
    float& outL = outStereo[2 * i];
    float& outR = outStereo[2 * i + 1];
    const float g = withVocal ? vocalGain_.next() : 0.0f;
    if (cursor_ < total && !available(active_, cursor_)) {
      // Khối tại vị trí phát chưa render (vừa tua tới): im lặng, đứng yên, sẵn thì fade vào lại.
      starved = true;
      fade_ = 0.0f;
      outL = 0.0f;
      outR = 0.0f;
      continue;
    }
    // Nhìn trước 5 ms: khối sắp tới chưa render (luồng render bị chậm) → fade ra như tạm dừng thay
    // vì tắt phụt (click), rồi đứng yên chờ.
    const bool aheadReady = available(active_, std::min(cursor_ + fadeFrames_, total - 1));
    if (wantPlay && !aheadReady) starved = true;
    fade_ = wantPlay && aheadReady ? std::min(1.0f, fade_ + fadeStep_)
                                   : std::max(0.0f, fade_ - fadeStep_);
    if (cursor_ >= total || fade_ <= 0.0f) {
      outL = 0.0f;
      outR = 0.0f;
      continue;
    }
    float l = 0.0f;
    float r = 0.0f;
    sampleAt(active_, cursor_, g, l, r);
    if (xfade_ > 0) {
      // Crossfade công suất đều (hai bản khác cao độ → gần như không tương quan).
      const float x = 1.0f - static_cast<float>(xfade_) / static_cast<float>(xfadeLen_);
      const float gNew = std::sin(kHalfPi * x);
      const float gOld = std::cos(kHalfPi * x);
      float ol = 0.0f;
      float orr = 0.0f;
      if (available(fadeFrom_, cursor_)) sampleAt(fadeFrom_, cursor_, g, ol, orr);
      l = gNew * l + gOld * ol;
      r = gNew * r + gOld * orr;
      if (--xfade_ == 0) {
        retire(nullptr, fadeFrom_);
        fadeFromValid_ = false;
      }
    }
    outL = l * fade_;
    outR = r * fade_;
    ++cursor_;
  }
  starved_.store(starved, std::memory_order_relaxed);

  if (cursor_ >= total && wantPlay) {
    playing_.store(false, std::memory_order_relaxed);
    ended_.store(true, std::memory_order_release);
  }
  // Không ghi đè vị trí nếu UI vừa yêu cầu tua (sẽ áp dụng ở block sau).
  if (seekRequest_.load(std::memory_order_relaxed) < 0) {
    position_.store(cursor_, std::memory_order_relaxed);
  }
}

}  // namespace tonecore
