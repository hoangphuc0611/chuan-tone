#include "stretch/peak_limiter.h"

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
      need_(static_cast<std::size_t>(lookahead_ + 1), 1.0f) {}

void PeakLimiter::reset() noexcept {
  std::fill(delay_.begin(), delay_.end(), 0.0f);
  std::fill(need_.begin(), need_.end(), 1.0f);
  gain_ = 1.0f;
  head_ = 0;
}

void PeakLimiter::process(const float* in, float* out, uint32_t frames) noexcept {
  const auto ch = static_cast<std::size_t>(channels_);
  const auto len = static_cast<uint32_t>(lookahead_ + 1);
  for (uint32_t f = 0; f < frames; ++f) {
    // Mẫu mới vào vị trí head_ (cũ nhất ra trước, ở head_ + 1).
    float peak = 0.0f;
    for (std::size_t c = 0; c < ch; ++c) peak = std::max(peak, std::abs(in[f * ch + c]));
    const uint32_t slot = head_;
    // Lấy mẫu cũ nhất (chậm L) ra trước khi ghi đè.
    const uint32_t oldest = (head_ + 1) % len;
    for (std::size_t c = 0; c < ch; ++c) delay_[slot * ch + c] = in[f * ch + c];
    need_[slot] = peak > ceiling_ ? ceiling_ / peak : 1.0f;
    float target = 1.0f;
    for (uint32_t k = 0; k < len; ++k) target = std::min(target, need_[k]);
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
