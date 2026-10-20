#include "player/beat_player.h"

#include <algorithm>

namespace tonecore {

namespace {
constexpr double kFadeSec = 0.005;  // 5 ms: đủ ngắn để không trễ, đủ dài để không "bụp"
constexpr float kInt16ToFloat = 1.0f / 32768.0f;
}  // namespace

BeatPlayer::BeatPlayer(uint32_t sampleRate)
    : fadeStep_(static_cast<float>(1.0 / (kFadeSec * sampleRate))) {}

BeatPlayer::~BeatPlayer() {
  Track* t = nullptr;
  while (incoming_.pop(t)) delete t;
  while (retired_.pop(t)) delete t;
  delete current_;
}

bool BeatPlayer::setTrack(std::unique_ptr<Track> track) {
  collectGarbage();
  if (!track) return false;
  const uint64_t frames = track->frames;
  Track* raw = track.release();
  if (!incoming_.push(raw)) {
    delete raw;
    return false;
  }
  playing_.store(false, std::memory_order_relaxed);
  seekRequest_.store(-1, std::memory_order_relaxed);
  position_.store(0, std::memory_order_relaxed);
  duration_.store(frames, std::memory_order_relaxed);
  ended_.store(false, std::memory_order_relaxed);
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
  Track* t = nullptr;
  while (retired_.pop(t)) delete t;
}

// [RT]
void BeatPlayer::adoptIncoming() noexcept {
  Track* t = nullptr;
  while (incoming_.pop(t)) {
    if (current_ != nullptr && !retired_.push(current_)) {
      // Hàng đợi trả về đầy (luồng điều khiển chưa dọn): giữ track cũ lại, trả track mới về
      // để không phải giải phóng trong audio thread. Không xảy ra với nhịp dùng bình thường.
      retired_.push(t);
      continue;
    }
    current_ = t;
    cursor_ = 0;
    fade_ = 0.0f;
  }
}

// [RT] Không cấp phát, không khóa.
void BeatPlayer::render(float* outStereo, uint32_t frames) noexcept {
  adoptIncoming();

  const int64_t seek = seekRequest_.exchange(-1, std::memory_order_relaxed);
  if (seek >= 0 && current_ != nullptr) {
    cursor_ = std::min<uint64_t>(static_cast<uint64_t>(seek), current_->frames);
  }

  const bool wantPlay = playing_.load(std::memory_order_relaxed) && current_ != nullptr;

  if (current_ == nullptr || (!wantPlay && fade_ <= 0.0f)) {
    std::fill(outStereo, outStereo + 2 * static_cast<std::size_t>(frames), 0.0f);
    return;
  }

  const int16_t* pcm = current_->pcm.data();
  const uint64_t total = current_->frames;
  for (uint32_t i = 0; i < frames; ++i) {
    fade_ = wantPlay ? std::min(1.0f, fade_ + fadeStep_) : std::max(0.0f, fade_ - fadeStep_);
    const float g = fade_ * kInt16ToFloat;
    if (cursor_ < total && g > 0.0f) {
      outStereo[2 * i] = static_cast<float>(pcm[2 * cursor_]) * g;
      outStereo[2 * i + 1] = static_cast<float>(pcm[2 * cursor_ + 1]) * g;
      ++cursor_;
    } else {
      outStereo[2 * i] = 0.0f;
      outStereo[2 * i + 1] = 0.0f;
    }
  }

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
