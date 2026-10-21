#include "dsp/resampler.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr double kZeroCrossings = 32.0;  // Z
constexpr double kKaiserBeta = 8.0;
constexpr double kRolloff = 0.92;      // dải thông tới 92% Nyquist (≈ 20,3 kHz ở 44,1 kHz)
constexpr int kTableResolution = 512;  // điểm bảng trên mỗi mẫu đầu vào

}  // namespace

Resampler::Resampler(double srcRate, double dstRate) : srcRate_(srcRate), dstRate_(dstRate) {
  const double scale = std::min(1.0, dstRate / srcRate);
  const double fc = 0.5 * scale * kRolloff;
  halfWidth_ = kZeroCrossings / (2.0 * fc);

  // Lập bảng h(x) với x ∈ [0, L] (h đối xứng).
  const auto size = static_cast<std::size_t>(std::ceil(halfWidth_ * kTableResolution)) + 2;
  table_.resize(size);
  for (std::size_t i = 0; i < size; ++i) {
    const double x = static_cast<double>(i) / kTableResolution;
    const double u = x / halfWidth_;
    if (u >= 1.0) {
      table_[i] = 0.0f;
      continue;
    }
    const double window = dsp::kaiserWindow(u, kKaiserBeta);
    table_[i] = static_cast<float>(2.0 * fc * dsp::sinc(2.0 * fc * x) * window);
  }
}

float Resampler::kernel(double x) const noexcept {
  const double pos = std::fabs(x) * kTableResolution;
  const auto i = static_cast<std::size_t>(pos);
  if (i + 1 >= table_.size()) return 0.0f;
  const auto frac = static_cast<float>(pos - static_cast<double>(i));
  return table_[i] + frac * (table_[i + 1] - table_[i]);
}

std::size_t Resampler::outputFrames(std::size_t frames) const {
  return static_cast<std::size_t>(std::ceil(static_cast<double>(frames) * dstRate_ / srcRate_));
}

namespace {

inline float toFloat(float v) {
  return v;
}
inline float toFloat(int16_t v) {
  return static_cast<float>(v);
}

template <typename T>
T fromFloat(float v);

template <>
float fromFloat<float>(float v) {
  return v;
}

template <>
int16_t fromFloat<int16_t>(float v) {
  const float r = std::nearbyint(v);
  return static_cast<int16_t>(std::clamp(r, -32768.0f, 32767.0f));
}

}  // namespace

std::vector<float> Resampler::process(const float* in, std::size_t frames, int channels) const {
  return processImpl(in, frames, channels);
}

std::vector<int16_t> Resampler::process(const int16_t* in, std::size_t frames, int channels) const {
  return processImpl(in, frames, channels);
}

template <typename T>
std::vector<T> Resampler::processImpl(const T* in, std::size_t frames, int channels) const {
  const std::size_t outFrames = outputFrames(frames);
  const auto ch = static_cast<std::size_t>(channels);
  std::vector<T> out(outFrames * ch, T{});
  if (frames == 0) return out;

  const double step = srcRate_ / dstRate_;  // khoảng cách giữa 2 mẫu ra, tính theo mẫu vào
  const auto reach = static_cast<long long>(std::ceil(halfWidth_));
  const auto last = static_cast<long long>(frames) - 1;
  std::vector<float> weights(static_cast<std::size_t>(2 * reach + 2));

  for (std::size_t j = 0; j < outFrames; ++j) {
    const double t = static_cast<double>(j) * step;
    const auto center = static_cast<long long>(std::floor(t));
    const long long n0 = std::max(0LL, center - reach + 1);
    const long long n1 = std::min(last, center + reach);
    if (n0 > n1) continue;

    // Tính trọng số một lần, dùng chung cho mọi kênh.
    const auto count = static_cast<std::size_t>(n1 - n0 + 1);
    for (std::size_t k = 0; k < count; ++k) {
      weights[k] = kernel(t - static_cast<double>(n0 + static_cast<long long>(k)));
    }
    for (std::size_t c = 0; c < ch; ++c) {
      float acc = 0.0f;
      const T* src = in + static_cast<std::size_t>(n0) * ch + c;
      for (std::size_t k = 0; k < count; ++k) acc += weights[k] * toFloat(src[k * ch]);
      out[j * ch + c] = fromFloat<T>(acc);
    }
  }
  return out;
}

}  // namespace tonecore
