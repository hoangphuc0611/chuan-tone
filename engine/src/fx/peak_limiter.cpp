#include "fx/peak_limiter.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

PeakLimiter::PeakLimiter(int channels, double sampleRate, float ceiling, double lookaheadMs,
                         double releaseMs)
    : channels_(std::max(1, channels)),
      lookahead_(std::max(1, static_cast<int>(lookaheadMs * 0.001 * sampleRate))),
      ceiling_(ceiling),
      // Sau L mẫu còn lại (1 − a)^L ≤ 1%.
      attack_(static_cast<float>(1.0 - std::pow(0.01, 1.0 / lookahead_))),
      release_(static_cast<float>(1.0 - std::exp(-1.0 / (releaseMs * 0.001 * sampleRate)))),
      delay_(static_cast<std::size_t>(lookahead_ + 1) * static_cast<std::size_t>(channels_), 0.0f),
      minVal_(static_cast<std::size_t>(lookahead_ + 1), 1.0f),
      minIdx_(static_cast<std::size_t>(lookahead_ + 1), 0) {}

void PeakLimiter::reset() noexcept {
  std::fill(delay_.begin(), delay_.end(), 0.0f);
  gain_ = 1.0f;
  head_ = 0;
  minHead_ = 0;
  minSize_ = 0;
  count_ = 0;
}

void PeakLimiter::process(const float* in, float* out, uint32_t frames) noexcept {
  const auto ch = static_cast<std::size_t>(channels_);
  const auto len = static_cast<uint32_t>(lookahead_ + 1);
  for (uint32_t f = 0; f < frames; ++f) {
    float peak = 0.0f;
    for (std::size_t c = 0; c < ch; ++c) peak = std::max(peak, std::abs(in[f * ch + c]));
    const uint32_t slot = head_;
    // Lấy mẫu cũ nhất (chậm L) ra trước khi ghi đè.
    const uint32_t oldest = head_ + 1 == len ? 0 : head_ + 1;
    for (std::size_t c = 0; c < ch; ++c) delay_[slot * ch + c] = in[f * ch + c];
    // r(n) vào hàng đợi đơn điệu: bỏ các phần tử cuối ≥ r(n) (không bao giờ là cực tiểu nữa),
    // bỏ phần tử đầu đã ra khỏi cửa sổ L + 1 mẫu → đầu hàng đợi là min r(k), k ∈ [n − L, n].
    const float need = peak > ceiling_ ? ceiling_ / peak : 1.0f;
    while (minSize_ > 0) {
      const uint32_t back = (minHead_ + minSize_ - 1) % len;
      if (minVal_[back] < need) break;
      --minSize_;
    }
    const uint32_t pos = (minHead_ + minSize_) % len;
    minVal_[pos] = need;
    minIdx_[pos] = count_;
    ++minSize_;
    if (minIdx_[minHead_] + len <= count_) {
      minHead_ = minHead_ + 1 == len ? 0 : minHead_ + 1;
      --minSize_;
    }
    ++count_;
    const float target = minVal_[minHead_];
    gain_ += (target - gain_) * (target < gain_ ? attack_ : release_);
    for (std::size_t c = 0; c < ch; ++c) {
      out[f * ch + c] = std::clamp(delay_[oldest * ch + c] * gain_, -1.0f, 1.0f);
    }
    head_ = oldest;
  }
}

void limitPeaks(std::vector<float>& x, int channels, double sampleRate, float ceiling) {
  const auto ch = static_cast<std::size_t>(std::max(1, channels));
  const std::size_t frames = x.size() / ch;
  PeakLimiter lim(channels, sampleRate, ceiling);
  const auto L = static_cast<std::size_t>(lim.lookahead());
  // Đệm L frame im lặng ở cuối để lấy ra đủ, bỏ L frame trễ ở đầu.
  std::vector<float> in(x);
  in.resize((frames + L) * ch, 0.0f);
  std::vector<float> out(in.size());
  lim.process(in.data(), out.data(), static_cast<uint32_t>(frames + L));
  std::copy(out.begin() + static_cast<std::ptrdiff_t>(L * ch), out.end(), x.begin());
}

}  // namespace tonecore
