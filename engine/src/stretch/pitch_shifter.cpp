#include "stretch/pitch_shifter.h"

#include <algorithm>
#include <cmath>

#include "dsp/kaiser.h"

namespace tonecore {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kPhases = 512;  // số pha của bảng nhân sinc đa pha (nội suy tuyến tính giữa 2 pha)
constexpr double kKaiserBeta = 8;  // dải chặn ~ −80 dB
constexpr double kRolloff = 0.97;  // tần số cắt = 0,97 · Nyquist (của tốc độ thấp hơn)
constexpr int kPrerollFrames = 2;  // số khung tính trước điểm bắt đầu cho pha ổn định
// Khung trước có đỉnh ≤ tỉ lệ này × đỉnh khung hiện tại (−100 dB) coi là im lặng.
constexpr float kSilenceRatio = 1e-5f;
constexpr float kSilencePower = kSilenceRatio * kSilenceRatio;
// Đỉnh phổ thấp hơn đỉnh lớn nhất của khung quá 90 dB thì bỏ qua (chỉ là nhiễu nền).
constexpr float kPeakFloorPower = 1e-9f;
// L + R triệt tiêu quá 30 dB so với tổng công suất (hai kênh ngược pha) → lấy kênh mạnh hơn làm
// phổ tham chiếu cho bin đó.
constexpr float kCancelPower = 1e-3f;

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
      phaseLock_(cfg.phaseLock),
      transients_(cfg.phaseLock && cfg.transients),
      n_(cfg.fftSize),
      hop_(cfg.synthesisHop),
      bins_(cfg.fftSize / 2 + 1),
      fft_(static_cast<std::size_t>(cfg.fftSize)),
      window_(static_cast<std::size_t>(cfg.fftSize)),
      detector_(channels, cfg.sampleRate, cfg.fftSize),
      frame_(static_cast<std::size_t>(cfg.fftSize)) {
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

  // Nhân sinc–Kaiser h(x) = 2·fc·sinc(2·fc·x)·kaiser(x / L), L = Z / (2·fc), lấy mẫu sẵn dạng đa
  // pha: hàng r ứng với phần lẻ φ = r / kPhases của vị trí đọc, cột t ứng với mẫu n = ⌊pos⌋ − K + 1
  // + t (x = pos − n = φ + K − 1 − t, K = ⌈L⌉). Mỗi mẫu ra chỉ nội suy một hàng trọng số rồi nhân
  // với 2K mẫu liền nhau của vòng đệm — vòng lặp liên tục, trình biên dịch tự vector hoá.
  const double fc = 0.5 * std::min(1.0, 1.0 / beta_) * kRolloff;
  const double halfWidth = static_cast<double>(cfg.sincZeros) / (2.0 * fc);
  halfTaps_ = static_cast<int>(std::ceil(halfWidth));
  taps_ = 2 * halfTaps_;
  const auto taps = static_cast<std::size_t>(taps_);
  poly_.resize((kPhases + 1) * taps);
  for (std::size_t r = 0; r <= kPhases; ++r) {
    const double phi = static_cast<double>(r) / kPhases;
    for (std::size_t t = 0; t < taps; ++t) {
      const double x = phi + static_cast<double>(halfTaps_ - 1) - static_cast<double>(t);
      poly_[r * taps + t] = std::abs(x) < halfWidth
                                ? static_cast<float>(2.0 * fc * dsp::sinc(2.0 * fc * x) *
                                                     dsp::kaiserWindow(x / halfWidth, kKaiserBeta))
                                : 0.0f;
    }
  }
  weights_.resize(taps);

  // Vòng đệm tín hiệu giãn đủ chứa: một khung + một bước + hai nửa nhân sinc. Thêm `taps_` ô
  // "gương" ở cuối (bản sao của các ô đầu) → đọc 2K mẫu liền nhau không phải quấn chỉ số.
  const auto need = static_cast<uint64_t>(n_ + hop_ + taps_ + 16);
  uint64_t cap = 1;
  while (cap < need) cap <<= 1;
  ringMask_ = cap - 1;
  ringCap_ = static_cast<std::size_t>(cap);
  const auto bins = static_cast<std::size_t>(bins_);
  for (int c = 0; c < channels_; ++c) {
    spec_.emplace_back(bins);
    ring_.emplace_back(static_cast<std::size_t>(cap) + taps, 0.0f);
    if (!phaseLock_) {
      prevPhase_.emplace_back(bins, 0.0f);
      prevMag_.emplace_back(bins, 0.0f);
      synthPhase_.emplace_back(bins, 0.0f);
    }
  }
  if (phaseLock_) {
    ref_.resize(bins);
    prevRef_.resize(bins);
    power_.resize(bins);
    unit_.resize(bins, 1.0f);
    prevUnit_.resize(bins, 1.0f);
    peaks_.reserve(bins / 2 + 1);
    peakTheta_.reserve(bins / 2 + 1);
  }
}

void PitchShifter::start(const float* in, uint64_t inFrames, uint64_t begin) noexcept {
  in_ = in;
  inFrames_ = inFrames;
  outPos_ = begin;
  if (bypass_) return;
  // Mẫu giãn thấp nhất cần cho frame ra đầu tiên, rồi khung tổng hợp đầu tiên phủ tới nó.
  const auto sMin =
      static_cast<int64_t>(std::floor(beta_ * static_cast<double>(begin))) - halfTaps_ - 1;
  const int64_t jMin = floorDiv(sMin - n_, hop_) + 1;
  nextFrame_ = jMin - kPrerollFrames;
  completeUpTo_ = nextFrame_ * hop_;
  zeroedUpTo_ = completeUpTo_;
  prevMax_ = 0.0f;
  if (transients_) {
    const double center = static_cast<double>(nextFrame_ * hop_ + n_ / 2) / beta_;
    detector_.start(in, inFrames, static_cast<int64_t>(std::floor(center)) - n_ / 2);
  }
  std::fill(prevUnit_.begin(), prevUnit_.end(), std::complex<float>(1.0f, 0.0f));
  for (auto& m : prevMag_) std::fill(m.begin(), m.end(), 0.0f);
  for (auto& r : ring_) std::fill(r.begin(), r.end(), 0.0f);
}

// Cửa sổ + FFT khung phân tích bắt đầu ở `analysis` cho từng kênh → spec_.
void PitchShifter::analyze(int64_t analysis) noexcept {
  const auto ch = static_cast<std::size_t>(channels_);
  for (std::size_t c = 0; c < ch; ++c) {
    for (int i = 0; i < n_; ++i) {
      const int64_t t = analysis + i;
      const float x = (t >= 0 && static_cast<uint64_t>(t) < inFrames_)
                          ? in_[static_cast<std::size_t>(t) * ch + c]
                          : 0.0f;
      frame_[static_cast<std::size_t>(i)] = x * window_[static_cast<std::size_t>(i)];
    }
    fft_.forward(frame_.data(), spec_[c].data());
  }
}

// Khoá pha identity trên phổ tham chiếu (ref_, power_ đã tính). Ghi unit_ (đọc prevUnit_).
void PitchShifter::lockPhases(float hopA, float frameMax) noexcept {
  const auto bins = static_cast<std::size_t>(bins_);
  peaks_.clear();
  const float floor = frameMax * kPeakFloorPower;
  for (std::size_t k = 2; k + 2 < bins; ++k) {
    const float p = power_[k];
    if (p > floor && p > power_[k - 1] && p >= power_[k + 1] && p > power_[k - 2] &&
        p >= power_[k + 2]) {
      peaks_.push_back(static_cast<int>(k));
    }
  }
  if (peaks_.empty()) {
    std::fill(unit_.begin(), unit_.end(), std::complex<float>(1.0f, 0.0f));
    return;
  }

  // Góc xoay của từng đỉnh, từ góc khung trước θ_trước(p) = arg(prevUnit_[p]).
  //   φ_s(p) = φ_a,trước(p) + θ_trước(p) + ω_tt·Hs  và  φ_a(p) ≡ φ_a,trước(p) + ω_tt·Ha
  //   ⇒ θ_p = θ_trước(p) + ω_tt·(Hs − Ha).
  const float omegaScale = kTwoPi / static_cast<float>(n_);
  const auto hs = static_cast<float>(hop_);
  peakTheta_.clear();
  for (const int p : peaks_) {
    const auto pp = static_cast<std::size_t>(p);
    const float omega = omegaScale * static_cast<float>(p);
    const float dphi = std::arg(ref_[pp] * std::conj(prevRef_[pp]));
    const float inst = omega + wrapPi(dphi - omega * hopA) / hopA;
    peakTheta_.push_back(wrapPi(std::arg(prevUnit_[pp]) + inst * (hs - hopA)));
  }

  // Vùng của mỗi đỉnh kéo tới bin thấp nhất giữa nó và đỉnh kế tiếp.
  std::size_t begin = 0;
  for (std::size_t i = 0; i < peaks_.size(); ++i) {
    std::size_t end = bins;  // không gồm
    if (i + 1 < peaks_.size()) {
      const auto a = static_cast<std::size_t>(peaks_[i]);
      const auto b = static_cast<std::size_t>(peaks_[i + 1]);
      std::size_t valley = a + 1;
      for (std::size_t k = a + 2; k < b; ++k) {
        if (power_[k] < power_[valley]) valley = k;
      }
      end = valley + 1;
    }
    const float theta = peakTheta_[i];
    const std::complex<float> u = std::polar(1.0f, theta);
    for (std::size_t k = begin; k < end; ++k) {
      unit_[k] = u;
    }
    begin = end;
  }
  // Bin 0 và Nyquist là số thực, không xoay được.
  unit_[0] = 1.0f;
  unit_[bins - 1] = 1.0f;
}

// Phase vocoder cơ bản (B1): mỗi kênh, mỗi bin tiến pha riêng.
void PitchShifter::propagateBasic(float hopA) noexcept {
  const float omegaScale = kTwoPi / static_cast<float>(n_);
  const auto hs = static_cast<float>(hop_);
  const auto bins = static_cast<std::size_t>(bins_);
  for (std::size_t c = 0; c < static_cast<std::size_t>(channels_); ++c) {
    auto& spec = spec_[c];
    auto& prev = prevPhase_[c];
    auto& prevMag = prevMag_[c];
    auto& synth = synthPhase_[c];
    float frameMax = 0.0f;
    float prevMax = 0.0f;
    for (std::size_t k = 0; k < bins; ++k) {
      frameMax = std::max(frameMax, std::abs(spec[k]));
      prevMax = std::max(prevMax, prevMag[k]);
    }
    const bool reset = prevMax <= kSilenceRatio * frameMax;
    for (std::size_t k = 0; k < bins; ++k) {
      const float mag = std::abs(spec[k]);
      const float phase = std::arg(spec[k]);
      if (reset) {
        synth[k] = phase;
      } else {
        const float omega = omegaScale * static_cast<float>(k);
        const float dev = wrapPi(phase - prev[k] - omega * hopA);
        synth[k] = wrapPi(synth[k] + (omega + dev / hopA) * hs);
      }
      prev[k] = phase;
      prevMag[k] = mag;
      spec[k] = std::polar(mag, synth[k]);
    }
  }
}

// Giữ transient: các bin trội của mọi onset trong khung [analysis, analysis + N) được căn về vị trí
// neo tại onset (xem pitch_shifter.h, bước 2b). Ghi đè unit_ của các bin đó.
void PitchShifter::applyTransients(int64_t analysis) noexcept {
  constexpr int kMaxActive = 4;
  detector_.ensure(analysis + n_);
  detector_.dropBefore(static_cast<double>(analysis));
  struct Active {
    const float* g;
    float w2;   // trọng số cửa sổ tại vị trí onset (chọn onset trội nhất cho từng bin)
    double ds;  // Δs: dời sớm bao nhiêu mẫu trên trục giãn
    std::complex<double> step;  // e^{i·ω_1·Δs}
    std::complex<double> cur;   // e^{i·ω_k·Δs} của bin đang xét (nhân dồn)
  };
  Active act[kMaxActive];
  int na = 0;
  const double end = static_cast<double>(analysis + n_);
  for (std::size_t i = 0; i < detector_.count() && na < kMaxActive; ++i) {
    const auto& o = detector_.onset(i);
    if (o.time >= end) break;
    const double d = o.time - static_cast<double>(analysis);
    if (d <= 0.0) continue;
    const float w = window_[static_cast<std::size_t>(d)];
    const double ds = (d - static_cast<double>(n_) / 2.0) * (1.0 - beta_);
    const std::complex<double> step =
        std::polar(1.0, 2.0 * dsp::kPi / static_cast<double>(n_) * ds);
    act[na++] = {o.g.data(), w * w, ds, step, step};  // cur bắt đầu ở bin 1
  }
  if (na == 0) return;
  for (std::size_t k = 1; k + 1 < static_cast<std::size_t>(bins_); ++k, [&] {
         for (int a = 0; a < na; ++a) act[a].cur *= act[a].step;
       }()) {
    int best = -1;
    float score = 0.0f;
    for (int a = 0; a < na; ++a) {
      const float s = act[a].g[k] * act[a].w2;
      if (s > score) {
        score = s;
        best = a;
      }
    }
    if (best < 0) continue;
    const float g = act[best].g[k];
    const std::complex<float> aligned(static_cast<float>(act[best].cur.real()),
                                      static_cast<float>(act[best].cur.imag()));
    if (g >= 1.0f) {
      unit_[k] = aligned;
    } else {
      // Trộn góc nền (khoá pha) với góc căn onset theo mức trội g: nội suy rồi chuẩn hoá vector
      // đơn vị (gần đúng nội suy góc; hai góc ngược hẳn nhau thì lấy góc căn).
      const std::complex<float> mix = (1.0f - g) * unit_[k] + g * aligned;
      const float len = std::abs(mix);
      unit_[k] = len > 1e-3f ? mix / len : aligned;
    }
  }
}

// IFFT, cửa sổ, cộng chồng từng kênh vào vòng đệm tại vị trí giãn `synthStart`.
void PitchShifter::synthesize(int64_t synthStart) noexcept {
  for (std::size_t c = 0; c < static_cast<std::size_t>(channels_); ++c) {
    fft_.inverse(spec_[c].data(), frame_.data());
    auto& ring = ring_[c];
    const auto taps = static_cast<std::size_t>(taps_);
    for (int i = 0; i < n_; ++i) {
      const auto idx = static_cast<std::size_t>(static_cast<uint64_t>(synthStart + i) & ringMask_);
      const float v =
          frame_[static_cast<std::size_t>(i)] * window_[static_cast<std::size_t>(i)] * olaScale_;
      ring[idx] += v;
      if (idx < taps) ring[idx + ringCap_] += v;  // ô gương
    }
  }
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
    for (auto& r : ring_) {
      r[idx] = 0.0f;
      if (idx < static_cast<std::size_t>(taps_)) r[idx + ringCap_] = 0.0f;
    }
  }
  zeroedUpTo_ = std::max(zeroedUpTo_, writeEnd);

  analyze(analysis);
  if (phaseLock_) {
    const auto bins = static_cast<std::size_t>(bins_);
    const auto ch = static_cast<std::size_t>(channels_);
    float frameMax = 0.0f;
    for (std::size_t k = 0; k < bins; ++k) {
      std::complex<float> sum = spec_[0][k];
      float power = std::norm(spec_[0][k]);
      std::size_t strongest = 0;
      for (std::size_t c = 1; c < ch; ++c) {
        sum += spec_[c][k];
        const float pc = std::norm(spec_[c][k]);
        power += pc;
        if (pc > std::norm(spec_[strongest][k])) strongest = c;
      }
      ref_[k] = std::norm(sum) < kCancelPower * power ? spec_[strongest][k] : sum;
      power_[k] = power;
      frameMax = std::max(frameMax, power);
    }
    if (prevMax_ <= kSilencePower * frameMax) {
      // Khung trước im lặng: pha khung trước vô nghĩa → lấy nguyên pha phân tích (θ = 0) cho cả
      // khung, mọi bin cùng lúc để giữ quan hệ pha giữa các bin.
      std::fill(unit_.begin(), unit_.end(), std::complex<float>(1.0f, 0.0f));
    } else {
      lockPhases(hopA, frameMax);
    }
    if (transients_) applyTransients(analysis);
    for (auto& spec : spec_) {
      for (std::size_t k = 0; k < bins; ++k) spec[k] *= unit_[k];
    }
    std::swap(ref_, prevRef_);
    std::swap(unit_, prevUnit_);  // unit_ được ghi lại toàn bộ ở khung sau
    prevMax_ = frameMax;
  } else {
    propagateBasic(hopA);
  }
  synthesize(synthStart);
  prevAnalysis_ = analysis;
  completeUpTo_ = (j + 1) * hop_;
}

// Nội suy sinc tại vị trí giãn `pos` cho mọi kênh (đã chắc chắn đủ mẫu hai bên).
void PitchShifter::interpolate(double pos, float* out) noexcept {
  const double fl = std::floor(pos);
  const double ph = (pos - fl) * kPhases;
  const auto r = std::min(static_cast<std::size_t>(ph), static_cast<std::size_t>(kPhases - 1));
  const auto f = static_cast<float>(ph - static_cast<double>(r));
  const auto taps = static_cast<std::size_t>(taps_);
  const float* row0 = poly_.data() + r * taps;
  const float* row1 = row0 + taps;
  float* w = weights_.data();
  for (std::size_t t = 0; t < taps; ++t) w[t] = row0[t] + f * (row1[t] - row0[t]);
  const auto base = static_cast<std::size_t>(
      static_cast<uint64_t>(static_cast<int64_t>(fl) - halfTaps_ + 1) & ringMask_);
  for (std::size_t c = 0; c < static_cast<std::size_t>(channels_); ++c) {
    const float* src = ring_[c].data() + base;
    float acc = 0.0f;
    for (std::size_t t = 0; t < taps; ++t) acc += src[t] * w[t];
    out[c] = acc;
  }
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
    const auto needed = static_cast<int64_t>(std::floor(pos)) + halfTaps_ + 1;
    while (completeUpTo_ <= needed) computeFrame();
    interpolate(pos, out + f * ch);
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
