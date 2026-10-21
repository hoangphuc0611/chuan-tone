#include "dsp/decimator.h"

#include <algorithm>
#include <stdexcept>

#include "dsp/kaiser.h"

namespace tonecore {

Decimator::Decimator(int factor, int taps, double cutoffOfOutputRate, double beta)
    : factor_(factor) {
  if (factor < 1 || taps < 1) throw std::invalid_argument("Decimator: tham số không hợp lệ");
  // Tần số cắt tính theo chu kỳ/mẫu đầu vào: fc = cutoff × (fs/M) / fs = cutoff / M.
  const double fc = cutoffOfOutputRate / factor;
  const auto n = static_cast<std::size_t>(taps);
  h_.resize(n);
  const double center = (static_cast<double>(n) - 1.0) / 2.0;
  double sum = 0.0;
  std::vector<double> tmp(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double x = static_cast<double>(i) - center;
    const double u = center > 0.0 ? x / (center + 1.0) : 0.0;
    tmp[i] = 2.0 * fc * dsp::sinc(2.0 * fc * x) * dsp::kaiserWindow(u, beta);
    sum += tmp[i];
  }
  // Chuẩn hoá độ lợi DC = 1.
  for (std::size_t i = 0; i < n; ++i) h_[i] = static_cast<float>(tmp[i] / sum);
  hist_.assign(2 * n, 0.0f);
}

void Decimator::reset() noexcept {
  std::fill(hist_.begin(), hist_.end(), 0.0f);
  pos_ = 0;
  phase_ = 0;
}

// [RT]
std::size_t Decimator::process(const float* in, std::size_t n, float* out) noexcept {
  const std::size_t taps = h_.size();
  const float* h = h_.data();
  std::size_t produced = 0;
  for (std::size_t i = 0; i < n; ++i) {
    hist_[pos_] = in[i];
    hist_[pos_ + taps] = in[i];
    pos_ = pos_ + 1 == taps ? 0 : pos_ + 1;
    if (++phase_ < factor_) continue;
    phase_ = 0;
    // hist_[pos_ .. pos_+taps) là N mẫu gần nhất theo thứ tự cũ → mới.
    const float* x = hist_.data() + pos_;
    float acc = 0.0f;
    for (std::size_t k = 0; k < taps; ++k) acc += h[k] * x[k];
    out[produced++] = acc;
  }
  return produced;
}

}  // namespace tonecore
