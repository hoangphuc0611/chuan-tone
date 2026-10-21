#include "pitch/f0_tracker.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

inline float hzToMidi(float hz) noexcept {
  return 69.0f + 12.0f * std::log2(hz / 440.0f);
}
inline float midiToHz(float midi) noexcept {
  return 440.0f * std::exp2((midi - 69.0f) / 12.0f);
}

}  // namespace

void F0Tracker::reset() noexcept {
  voiced_ = false;
  run_ = 0;
  onsetPending_ = false;
  lastMidi_ = 0.0f;
  pendingCount_ = 0;
  histCount_ = 0;
}

// [RT]
TrackedPitch F0Tracker::update(const PitchEstimate& est, float rmsDb, float zcr) noexcept {
  // ---- 1. Hữu thanh hay vô thanh ----
  const float clarityNeed = voiced_ ? cfg_.clarityOff : cfg_.clarityOn;
  bool voicedNow = est.f0 > 0.0f && rmsDb > cfg_.silenceDb && est.clarity >= clarityNeed;
  if (voicedNow && zcr > cfg_.zcrMax && est.clarity < cfg_.zcrClarity) voicedNow = false;

  if (!voicedNow) {
    voiced_ = false;
    run_ = 0;
    onsetPending_ = false;
    pendingCount_ = 0;
    histCount_ = 0;
    return {};
  }

  // ---- 2. Chống nhảy bất thường (thường là lỗi quãng 8) ----
  const float midi = hzToMidi(est.f0);
  if (!voiced_ && est.clarity < cfg_.onsetClarity) {
    const bool confirmed = onsetPending_ && std::fabs(midi - onsetMidi_) < 1.0f;
    if (!confirmed) {
      onsetPending_ = true;
      onsetMidi_ = midi;
      return {};
    }
  }
  onsetPending_ = false;
  float accepted = midi;
  const bool established = voiced_ && run_ >= cfg_.jumpConfirmFrames;
  const float jump = voiced_ ? std::fabs(midi - lastMidi_) * 100.0f : 0.0f;
  if (voiced_ && !established && jump > cfg_.jumpCents) {
    histCount_ = 0;  // đoạn non: tin giá trị mới, bỏ lịch sử
  } else if (established) {
    if (jump > cfg_.jumpCents) {
      // Chỉ khung "rõ" (clarity ≥ ngưỡng bắt đầu) mới được tính là bằng chứng cho bước nhảy:
      // ở chỗ chuyển nốt, MPM hay cho bội số với clarity thấp kéo dài vài khung.
      const bool strong = est.clarity >= cfg_.clarityOn;
      if (!strong) {
        pendingCount_ = 0;
      } else if (pendingCount_ > 0 && std::fabs(midi - pendingMidi_) < 1.0f) {
        ++pendingCount_;
      } else {
        pendingCount_ = 1;
      }
      pendingMidi_ = midi;
      if (pendingCount_ < cfg_.jumpConfirmFrames) {  // gồm cả trường hợp pendingCount_ = 0
        accepted = lastMidi_;  // giữ giá trị cũ trong lúc chờ xác nhận
      } else {
        pendingCount_ = 0;
        histCount_ = 0;  // bước nhảy thật (vd. hát quãng 8): bỏ lịch sử để median không kéo lại
      }
    } else {
      pendingCount_ = 0;
    }
  }
  voiced_ = true;
  ++run_;
  lastMidi_ = accepted;

  // ---- 3. Lọc trung vị ----
  const int want = std::clamp(cfg_.medianFrames, 1, 5);
  if (histCount_ < want) {
    hist_[histCount_++] = accepted;
  } else {
    for (int i = 1; i < want; ++i) hist_[i - 1] = hist_[i];
    hist_[want - 1] = accepted;
  }
  float sorted[5];
  std::copy(hist_, hist_ + histCount_, sorted);
  std::sort(sorted, sorted + histCount_);
  // Số phần tử chẵn (lúc mới bắt đầu): lấy phần tử gần giá trị mới nhất hơn.
  float med = sorted[histCount_ / 2];
  if (histCount_ % 2 == 0) {
    const float lo = sorted[histCount_ / 2 - 1];
    med = std::fabs(lo - accepted) < std::fabs(med - accepted) ? lo : med;
  }

  TrackedPitch out;
  out.voiced = true;
  out.midi = med;
  out.f0 = midiToHz(med);
  out.confidence = est.clarity;
  return out;
}

}  // namespace tonecore
