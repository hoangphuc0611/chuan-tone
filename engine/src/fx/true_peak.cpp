#include "fx/true_peak.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr double kBeta = 8.0;

double sinc(double x) {
  return x == 0.0 ? 1.0 : std::sin(dsp::kPi * x) / (dsp::kPi * x);
}

}  // namespace

TruePeakDetector::TruePeakDetector(int channels)
    : channels_(std::max(1, channels)),
      hist_(static_cast<std::size_t>(kTaps) * static_cast<std::size_t>(std::max(1, channels)),
            0.0f) {
  // Điểm xen giữa tại n + p/4 dùng các mẫu n − 7 … n + 8 (chỉ số j = 0 … 15 ứng với n − 7 + j):
  // h_p(j) = sinc(t) · kaiser(t / 8), t = (j − 7) − p/4. Chuẩn hoá tổng = 1 (giữ DC).
  for (int p = 1; p <= 3; ++p) {
    double sum = 0.0;
    auto& h = phase_[static_cast<std::size_t>(p - 1)];
    for (int j = 0; j < kTaps; ++j) {
      const double t = static_cast<double>(j - (kDelay - 1)) - p / 4.0;
      const double r = t / static_cast<double>(kDelay);
      const double w = dsp::kaiserWindow(r, kBeta);
      h[static_cast<std::size_t>(j)] = static_cast<float>(sinc(t) * w);
      sum += static_cast<double>(h[static_cast<std::size_t>(j)]);
    }
    for (float& v : h) v = static_cast<float>(static_cast<double>(v) / sum);
  }
}

void TruePeakDetector::reset() noexcept {
  std::fill(hist_.begin(), hist_.end(), 0.0f);
  head_ = 0;
}

void TruePeakDetector::process(const float* in, float* peaks, uint32_t frames) noexcept {
  const auto ch = static_cast<std::size_t>(channels_);
  for (uint32_t f = 0; f < frames; ++f) {
    for (std::size_t c = 0; c < ch; ++c) hist_[head_ * ch + c] = in[f * ch + c];
    head_ = head_ + 1 == static_cast<uint32_t>(kTaps) ? 0 : head_ + 1;
    // head_ giờ trỏ frame cũ nhất (n − 7); frame n ở vị trí j = kDelay − 1.
    float peak = 0.0f;
    for (std::size_t c = 0; c < ch; ++c) {
      auto at = [&](int j) {
        const uint32_t idx = (head_ + static_cast<uint32_t>(j)) % static_cast<uint32_t>(kTaps);
        return hist_[idx * ch + c];
      };
      peak = std::max(peak, std::fabs(at(kDelay - 1)));
      for (const auto& h : phase_) {
        float y = 0.0f;
        for (int j = 0; j < kTaps; ++j) y += h[static_cast<std::size_t>(j)] * at(j);
        peak = std::max(peak, std::fabs(y));
      }
    }
    peaks[f] = peak;
  }
}

float measureTruePeak(const float* x, std::size_t frames, int channels) {
  const auto ch = static_cast<std::size_t>(std::max(1, channels));
  TruePeakDetector det(channels);
  constexpr std::size_t kChunk = 4096;
  std::vector<float> peaks(kChunk);
  std::vector<float> zeros(static_cast<std::size_t>(TruePeakDetector::kDelay) * ch, 0.0f);
  float best = 0.0f;
  for (std::size_t at = 0; at < frames; at += kChunk) {
    const auto n = static_cast<uint32_t>(std::min(kChunk, frames - at));
    det.process(x + at * ch, peaks.data(), n);
    for (uint32_t i = 0; i < n; ++i) best = std::max(best, peaks[i]);
  }
  det.process(zeros.data(), peaks.data(), TruePeakDetector::kDelay);  // đẩy nốt các frame cuối
  for (int i = 0; i < TruePeakDetector::kDelay; ++i) best = std::max(best, peaks[i]);
  return best;
}

TruePeakLimiter::TruePeakLimiter(int channels, double sampleRate, float ceiling, double lookaheadMs,
                                 double releaseMs)
    : channels_(std::max(1, channels)),
      detector_(channels),
      limiter_(channels, sampleRate, ceiling, lookaheadMs, releaseMs),
      delay_(static_cast<std::size_t>(TruePeakDetector::kDelay) *
                 static_cast<std::size_t>(std::max(1, channels)),
             0.0f) {}

void TruePeakLimiter::process(const float* in, float* out, uint32_t frames) {
  const auto ch = static_cast<std::size_t>(channels_);
  peaks_.resize(frames);
  aligned_.resize(frames * ch);
  detector_.process(in, peaks_.data(), frames);
  // Mẫu chậm kDelay frame cho khớp với đỉnh vừa dò.
  for (uint32_t f = 0; f < frames; ++f) {
    for (std::size_t c = 0; c < ch; ++c) {
      aligned_[f * ch + c] = delay_[head_ * ch + c];
      delay_[head_ * ch + c] = in[f * ch + c];
    }
    head_ = head_ + 1 == static_cast<uint32_t>(TruePeakDetector::kDelay) ? 0 : head_ + 1;
  }
  limiter_.process(aligned_.data(), out, frames, peaks_.data());
}

}  // namespace tonecore
