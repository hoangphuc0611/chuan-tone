#include "stretch/pitch_shifter.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kTableRes = 512;     // số điểm bảng nhân sinc trên mỗi mẫu
constexpr double kKaiserBeta = 8;  // dải chặn ~ −80 dB
constexpr double kRolloff = 0.97;  // tần số cắt = 0,97 · Nyquist (của tốc độ thấp hơn)
constexpr int kPrerollFrames = 2;  // số khung tính trước điểm bắt đầu cho pha ổn định
// Khung trước có đỉnh ≤ tỉ lệ này × đỉnh khung hiện tại (−100 dB) coi là im lặng.
constexpr float kSilenceRatio = 1e-5f;

inline float wrapPi(float x) noexcept {
  return x - kTwoPi * std::nearbyint(x / kTwoPi);
}

inline int64_t floorDiv(int64_t a, int64_t b) noexcept {
  const int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

}  // namespace

PitchShifter::PitchShifter(int channels, double semitones, const PitchShiftConfig& cfg)
    : channels_(std::max(1, channels)),
      beta_(std::pow(2.0, semitones / 12.0)),
      bypass_(semitones == 0.0),
      n_(cfg.fftSize),
      hop_(cfg.synthesisHop),
      bins_(cfg.fftSize / 2 + 1),
      fft_(static_cast<std::size_t>(cfg.fftSize)),
      window_(static_cast<std::size_t>(cfg.fftSize)),
      frame_(static_cast<std::size_t>(cfg.fftSize)),
      spec_(static_cast<std::size_t>(cfg.fftSize / 2 + 1)) {
  // Hann tuần hoàn: tổng w² dịch theo bước Hs là hằng số khi N/Hs ≥ 3.
  for (int i = 0; i < n_; ++i) {
    window_[static_cast<std::size_t>(i)] = static_cast<float>(
        0.5 - 0.5 * std::cos(2.0 * dsp::kPi * static_cast<double>(i) / static_cast<double>(n_)));
  }
  double sum = 0.0;
  for (int i = 0; i < hop_; ++i) {
    for (int q = i; q < n_; q += hop_) {
      const double w = static_cast<double>(window_[static_cast<std::size_t>(q)]);
      sum += w * w;
    }
  }
  olaScale_ = static_cast<float>(static_cast<double>(hop_) / sum);

  // Nhân sinc–Kaiser: h(x) = 2·fc·sinc(2·fc·x)·kaiser(x / L), L = Z / (2·fc).
  const double fc = 0.5 * std::min(1.0, 1.0 / beta_) * kRolloff;
  halfWidth_ = static_cast<double>(cfg.sincZeros) / (2.0 * fc);
  const auto tableLen = static_cast<std::size_t>(std::ceil(halfWidth_ * kTableRes)) + 2;
  kernel_.resize(tableLen);
  for (std::size_t i = 0; i < tableLen; ++i) {
    const double x = static_cast<double>(i) / kTableRes;
    kernel_[i] = static_cast<float>(2.0 * fc * dsp::sinc(2.0 * fc * x) *
                                    dsp::kaiserWindow(x / halfWidth_, kKaiserBeta));
  }

  // Vòng đệm tín hiệu giãn đủ chứa: một khung + một bước + hai nửa nhân sinc.
  const auto need = static_cast<uint64_t>(n_ + hop_ + 2 * static_cast<int>(halfWidth_) + 16);
  uint64_t cap = 1;
  while (cap < need) cap <<= 1;
  ringMask_ = cap - 1;
  const auto bins = static_cast<std::size_t>(bins_);
  for (int c = 0; c < channels_; ++c) {
    prevPhase_.emplace_back(bins, 0.0f);
    prevMag_.emplace_back(bins, 0.0f);
    synthPhase_.emplace_back(bins, 0.0f);
    ring_.emplace_back(static_cast<std::size_t>(cap), 0.0f);
  }
}

void PitchShifter::start(const float* in, uint64_t inFrames, uint64_t begin) noexcept {
  in_ = in;
  inFrames_ = inFrames;
  outPos_ = begin;
  if (bypass_) return;
  // Mẫu giãn thấp nhất cần cho frame ra đầu tiên, rồi khung tổng hợp đầu tiên phủ tới nó.
  const auto sMin =
      static_cast<int64_t>(std::floor(beta_ * static_cast<double>(begin) - halfWidth_)) - 1;
  const int64_t jMin = floorDiv(sMin - n_, hop_) + 1;
  nextFrame_ = jMin - kPrerollFrames;
  completeUpTo_ = nextFrame_ * hop_;
  zeroedUpTo_ = completeUpTo_;
  for (auto& m : prevMag_) std::fill(m.begin(), m.end(), 0.0f);
  for (auto& r : ring_) std::fill(r.begin(), r.end(), 0.0f);
}

// Tính khung tổng hợp `nextFrame_` và cộng vào vòng đệm.
void PitchShifter::computeFrame() noexcept {
  const int64_t j = nextFrame_++;
  const int64_t synthStart = j * hop_;
  const double center = static_cast<double>(synthStart + n_ / 2) / beta_;
  const auto analysis = static_cast<int64_t>(std::llround(center - n_ / 2));
  const auto hopA = static_cast<float>(analysis - prevAnalysis_);

  // Xoá các ô vòng đệm sắp được cộng lần đầu (chúng đang giữ dữ liệu cũ đã đọc xong).
  const int64_t writeEnd = synthStart + n_;
  for (int64_t s = zeroedUpTo_; s < writeEnd; ++s) {
    const auto idx = static_cast<std::size_t>(static_cast<uint64_t>(s) & ringMask_);
    for (auto& r : ring_) r[idx] = 0.0f;
  }
  zeroedUpTo_ = std::max(zeroedUpTo_, writeEnd);

  const float omegaScale = kTwoPi / static_cast<float>(n_);
  const auto hs = static_cast<float>(hop_);
  for (int c = 0; c < channels_; ++c) {
    for (int i = 0; i < n_; ++i) {
      const int64_t t = analysis + i;
      const float x = (t >= 0 && static_cast<uint64_t>(t) < inFrames_)
                          ? in_[static_cast<std::size_t>(t) * static_cast<std::size_t>(channels_) +
                                static_cast<std::size_t>(c)]
                          : 0.0f;
      frame_[static_cast<std::size_t>(i)] = x * window_[static_cast<std::size_t>(i)];
    }
    fft_.forward(frame_.data(), spec_.data());
    auto& prev = prevPhase_[static_cast<std::size_t>(c)];
    auto& prevMag = prevMag_[static_cast<std::size_t>(c)];
    auto& synth = synthPhase_[static_cast<std::size_t>(c)];
    // Khung trước im lặng (đỉnh thấp hơn khung này > 100 dB): pha khung trước vô nghĩa → lấy pha
    // phân tích cho CẢ khung. Đặt lại cùng lúc mọi bin để giữ quan hệ pha giữa các bin của cùng một
    // nốt; tính tiếp từ pha "rác" (hoặc đặt lại từng bin ở các khung khác nhau khi β ≠ 1) làm quan
    // hệ đó lệch mãi về sau → các khung chồng nhau triệt tiêu (nhỏ tiếng) và lệch thời gian.
    float frameMax = 0.0f;
    float prevMax = 0.0f;
    for (std::size_t k = 0; k < static_cast<std::size_t>(bins_); ++k) {
      frameMax = std::max(frameMax, std::abs(spec_[k]));
      prevMax = std::max(prevMax, prevMag[k]);
    }
    const bool reset = prevMax <= kSilenceRatio * frameMax;
    for (int k = 0; k < bins_; ++k) {
      const auto kk = static_cast<std::size_t>(k);
      const float mag = std::abs(spec_[kk]);
      const float phase = std::arg(spec_[kk]);
      if (reset) {
        synth[kk] = phase;
      } else {
        // Tần số tức thời của bin k (rad/mẫu) từ chênh pha giữa hai khung phân tích.
        const float omega = omegaScale * static_cast<float>(k);
        const float dev = wrapPi(phase - prev[kk] - omega * hopA);
        synth[kk] = wrapPi(synth[kk] + (omega + dev / hopA) * hs);
      }
      prev[kk] = phase;
      prevMag[kk] = mag;
      spec_[kk] = std::polar(mag, synth[kk]);
    }
    fft_.inverse(spec_.data(), frame_.data());
    auto& ring = ring_[static_cast<std::size_t>(c)];
    for (int i = 0; i < n_; ++i) {
      const auto idx = static_cast<std::size_t>(static_cast<uint64_t>(synthStart + i) & ringMask_);
      ring[idx] +=
          frame_[static_cast<std::size_t>(i)] * window_[static_cast<std::size_t>(i)] * olaScale_;
    }
  }
  prevAnalysis_ = analysis;
  completeUpTo_ = (j + 1) * hop_;
}

// Nội suy sinc tại vị trí giãn `pos` (đã chắc chắn đủ mẫu hai bên).
float PitchShifter::interpolate(const std::vector<float>& ring, double pos) const noexcept {
  const auto lo = static_cast<int64_t>(std::ceil(pos - halfWidth_));
  const auto hi = static_cast<int64_t>(std::floor(pos + halfWidth_));
  float acc = 0.0f;
  for (int64_t n = lo; n <= hi; ++n) {
    const double t = std::abs(pos - static_cast<double>(n)) * kTableRes;
    const auto i = static_cast<std::size_t>(t);
    if (i + 1 >= kernel_.size()) continue;
    const auto frac = static_cast<float>(t - static_cast<double>(i));
    const float h = kernel_[i] + frac * (kernel_[i + 1] - kernel_[i]);
    acc += ring[static_cast<std::size_t>(static_cast<uint64_t>(n) & ringMask_)] * h;
  }
  return acc;
}

void PitchShifter::pull(float* out, uint32_t frames) noexcept {
  const auto ch = static_cast<std::size_t>(channels_);
  if (bypass_) {
    for (uint32_t f = 0; f < frames; ++f, ++outPos_) {
      for (std::size_t c = 0; c < ch; ++c) {
        out[f * ch + c] =
            outPos_ < inFrames_ ? in_[static_cast<std::size_t>(outPos_) * ch + c] : 0.0f;
      }
    }
    return;
  }
  for (uint32_t f = 0; f < frames; ++f, ++outPos_) {
    const double pos = beta_ * static_cast<double>(outPos_);
    const auto needed = static_cast<int64_t>(std::floor(pos + halfWidth_)) + 1;
    while (completeUpTo_ <= needed) computeFrame();
    for (std::size_t c = 0; c < ch; ++c) out[f * ch + c] = interpolate(ring_[c], pos);
  }
}

std::vector<float> pitchShiftBuffer(const float* in, uint64_t frames, int channels,
                                    double semitones, const PitchShiftConfig& cfg) {
  std::vector<float> out(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
  PitchShifter ps(channels, semitones, cfg);
  ps.start(in, frames, 0);
  constexpr uint32_t kChunk = 4096;
  for (uint64_t pos = 0; pos < frames; pos += kChunk) {
    const auto n = static_cast<uint32_t>(std::min<uint64_t>(kChunk, frames - pos));
    ps.pull(out.data() + static_cast<std::size_t>(pos) * static_cast<std::size_t>(channels), n);
  }
  return out;
}

}  // namespace tonecore
