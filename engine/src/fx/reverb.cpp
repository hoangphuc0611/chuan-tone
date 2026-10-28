#include "fx/reverb.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

namespace {

constexpr double kSmoothSec = 0.020;
constexpr float kInvSqrt8 = 0.35355339059327373f;

// Mỗi kiểu phòng: khoảng độ dài 8 dòng FDN (ms), 4 all-pass khuếch tán (ms) và độ khuếch tán g.
// Room: phòng nhỏ; Hall: hội trường, dòng dài; Plate: dòng ngắn hơn, sáng (damping thấp ở preset).
// Đo ở B3 (docs/fx-results.md): dòng dài hơn (tổng 0,3–0,6 s) → mật độ mode cao hơn; g = 0,5 —
// g 0,6–0,75 làm phổ đuôi nhô thêm 1–5 dB (các all-pass nối tiếp tự tô màu răng lược).
struct TypeShape {
  double lineMinMs;
  double lineMaxMs;
  double diffMs[Reverb::kDiffusers];
  float diffusion;
};
constexpr TypeShape kShapes[] = {
    {23.0, 61.0, {2.2, 3.3, 5.1, 7.9}, 0.5f},    // Room
    {41.0, 113.0, {4.1, 6.3, 9.7, 15.1}, 0.5f},  // Hall
    {29.0, 73.0, {1.9, 3.3, 5.3, 8.9}, 0.5f},    // Plate
};
constexpr double kMaxLineMs = 113.0;
constexpr double kMaxDiffMs = 15.1;
// RT60 cao tần (decay·(1 − damping)) đặt tại tần số này: tiếng vang tối dần từ vùng 4 kHz như phòng
// thật (đặt ở Nyquist thì vùng 4–8 kHz gần như không bị giảm — đo ở B3).
constexpr double kDampHz = 4000.0;

// Dấu: đầu vào (không trùng hàng Walsh nào → kích đều các mode), hai đầu ra (hai hàng Walsh khác
// nhau → trực giao).
constexpr float kInSign[Reverb::kLines] = {1, 1, 1, -1, 1, -1, -1, 1};
constexpr float kOutL[Reverb::kLines] = {1, -1, 1, -1, 1, -1, 1, -1};
constexpr float kOutR[Reverb::kLines] = {1, 1, -1, -1, 1, 1, -1, -1};

bool isPrime(uint32_t n) {
  if (n < 2) return false;
  for (uint32_t d = 2; d * d <= n; ++d) {
    if (n % d == 0) return false;
  }
  return true;
}

uint32_t pow2AtLeast(uint32_t n) {
  uint32_t c = 1;
  while (c < n) c <<= 1;
  return c;
}

// Biến đổi Walsh–Hadamard nhanh 8 điểm tại chỗ (chưa chia √8).
inline void fwht8(float* v) noexcept {
  for (int h = 1; h < 8; h <<= 1) {
    for (int i = 0; i < 8; i += 2 * h) {
      for (int j = i; j < i + h; ++j) {
        const float a = v[j];
        const float b = v[j + h];
        v[j] = a + b;
        v[j + h] = a - b;
      }
    }
  }
}

}  // namespace

Reverb::Reverb(double sampleRate)
    : fs_(sampleRate),
      smooth_(static_cast<float>(1.0 - std::exp(-1.0 / (kSmoothSec * sampleRate)))) {
  // Cấp đủ cho kiểu dài nhất → đổi kiểu không cần cấp phát lại.
  const auto lineCap = pow2AtLeast(static_cast<uint32_t>(kMaxLineMs * 1e-3 * sampleRate) + 64);
  for (auto& l : lines_) {
    l.buf.assign(lineCap, 0.0f);
    l.mask = lineCap - 1;
  }
  const auto diffCap = pow2AtLeast(static_cast<uint32_t>(kMaxDiffMs * 1e-3 * sampleRate) + 8);
  for (auto& d : diff_) {
    d.buf.assign(diffCap, 0.0f);
    d.mask = diffCap - 1;
  }
  const auto preCap = pow2AtLeast(static_cast<uint32_t>(kMaxPreDelaySec * sampleRate) + 8);
  pre_.assign(preCap, 0.0f);
  preMask_ = preCap - 1;
  setParams(ReverbParams{});
  mix_ = mixTarget_;
}

void Reverb::configure(ReverbType type) noexcept {
  const TypeShape& s = kShapes[static_cast<int>(type)];
  uint32_t prev = 0;
  for (int i = 0; i < kLines; ++i) {
    const double ms =
        s.lineMinMs * std::pow(s.lineMaxMs / s.lineMinMs, static_cast<double>(i) / (kLines - 1));
    auto n = static_cast<uint32_t>(std::lround(ms * 1e-3 * fs_));
    n = std::max(n, prev + 1);
    while (!isPrime(n)) ++n;
    len_[static_cast<std::size_t>(i)] = n;
    prev = n;
  }
  for (int i = 0; i < kDiffusers; ++i) {
    auto& d = diff_[static_cast<std::size_t>(i)];
    d.len = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(s.diffMs[i] * 1e-3 * fs_)));
  }
  diffGain_ = s.diffusion;
}

void Reverb::updateAbsorption() noexcept {
  const double tLow = std::max(0.05, static_cast<double>(p_.decaySec));
  const double damp = std::clamp(static_cast<double>(p_.damping), 0.0, 0.9);
  const double tHigh = tLow * (1.0 - damp);
  const double cw = std::cos(2.0 * 3.14159265358979323846 * std::min(kDampHz, 0.45 * fs_) / fs_);
  float energy = 0.0f;
  for (std::size_t i = 0; i < kLines; ++i) {
    const double m = static_cast<double>(len_[i]);
    const double c = std::pow(10.0, -3.0 * m / (fs_ * tLow));
    // Tỉ lệ độ lợi tại kDampHz / DC cần có: r = 10^(−3m/(fs·T_cao)) / c. Một cực
    // |H(ω)|/H(0) = (1 − b)/√(1 − 2b·cos ω + b²) = r  ⇔  (1 − r²)·b² − 2(1 − r²·cos ω)·b + (1 − r²)
    // = 0 → nghiệm trong (0, 1).
    const double r = std::pow(10.0, -3.0 * m / (fs_ * tHigh)) / c;
    double b = 0.0;
    if (r < 1.0 - 1e-9) {
      const double a2 = 1.0 - r * r;
      const double a1 = 1.0 - r * r * cw;
      b = (a1 - std::sqrt(std::max(0.0, a1 * a1 - a2 * a2))) / a2;
    }
    c_[i] = static_cast<float>(c);
    b_[i] = static_cast<float>(b);
    // Năng lượng mỗi đơn vị bơm vào (đã qua hấp thụ ngay lần đọc đầu): Σ_{k≥1} c^(2k) = c²/(1 −
    // c²).
    energy += static_cast<float>(c * c / (1.0 - c * c));
  }
  inGain_ = 1.0f / std::sqrt(energy / static_cast<float>(kLines));
}

void Reverb::setParams(const ReverbParams& p) noexcept {
  if (!configured_ || p.type != p_.type) {
    configure(p.type);
    configured_ = true;
  }
  p_ = p;
  preLen_ = static_cast<uint32_t>(std::lround(
      std::clamp(static_cast<double>(p.preDelayMs) * 1e-3, 0.0, kMaxPreDelaySec) * fs_));
  updateAbsorption();
  mixTarget_ = std::max(0.0f, p.mix);
}

void Reverb::reset() noexcept {
  for (auto& l : lines_) std::fill(l.buf.begin(), l.buf.end(), 0.0f);
  for (auto& d : diff_) std::fill(d.buf.begin(), d.buf.end(), 0.0f);
  std::fill(pre_.begin(), pre_.end(), 0.0f);
  state_.fill(0.0f);
  mix_ = mixTarget_;
}

// [RT]
void Reverb::process(const float* in, float* outL, float* outR, uint32_t n) noexcept {
  float v[kLines];
  for (uint32_t t = 0; t < n; ++t) {
    // 1. Pre-delay.
    pre_[pos_ & preMask_] = in[t];
    float x = pre_[(pos_ - preLen_) & preMask_];
    // 2. All-pass khuếch tán: w = x + g·w[n−M], y = w[n−M] − g·w.
    for (auto& d : diff_) {
      const float wd = d.buf[(pos_ - d.len) & d.mask];
      const float w = x + diffGain_ * wd;
      d.buf[pos_ & d.mask] = w;
      x = wd - diffGain_ * w;
    }
    // 3–4. Đọc 8 dòng, hấp thụ, lấy ra L / R.
    float l = 0.0f;
    float r = 0.0f;
    for (std::size_t i = 0; i < kLines; ++i) {
      const float y = lines_[i].buf[(pos_ - len_[i]) & lines_[i].mask];
      state_[i] = c_[i] * (1.0f - b_[i]) * y + b_[i] * state_[i];
      v[i] = state_[i];
      l += kOutL[i] * v[i];
      r += kOutR[i] * v[i];
    }
    // 5. Ma trận Hadamard trực giao, ghi lại các dòng cùng tiếng vào.
    fwht8(v);
    const float xin = inGain_ * x;
    for (std::size_t i = 0; i < kLines; ++i) {
      lines_[i].buf[pos_ & lines_[i].mask] = kInvSqrt8 * v[i] + kInSign[i] * xin;
    }
    ++pos_;
    mix_ += smooth_ * (mixTarget_ - mix_);
    outL[t] = mix_ * kInvSqrt8 * l;
    outR[t] = mix_ * kInvSqrt8 * r;
  }
}

}  // namespace tonecore
