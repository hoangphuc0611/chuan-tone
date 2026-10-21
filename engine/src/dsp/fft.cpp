#include "dsp/fft.h"

#include <cmath>
#include <stdexcept>

namespace tonecore {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isPowerOfTwo(std::size_t n) {
  return n != 0 && (n & (n - 1)) == 0;
}

}  // namespace

RealFft::RealFft(std::size_t n) : n_(n), half_(n / 2) {
  if (n < 4 || !isPowerOfTwo(n)) throw std::invalid_argument("RealFft: N phải là 2^k, N ≥ 4");

  // Bảng đảo bit cho FFT phức kích thước N/2.
  std::size_t bits = 0;
  while ((std::size_t{1} << bits) < half_) ++bits;
  bitrev_.resize(half_);
  for (std::size_t i = 0; i < half_; ++i) {
    std::size_t r = 0;
    for (std::size_t b = 0; b < bits; ++b) {
      if ((i >> b) & 1u) r |= std::size_t{1} << (bits - 1 - b);
    }
    bitrev_[i] = static_cast<uint32_t>(r);
  }

  twiddle_.resize(half_ / 2 > 0 ? half_ / 2 : 1);
  for (std::size_t j = 0; j < twiddle_.size(); ++j) {
    const double a = -2.0 * kPi * static_cast<double>(j) / static_cast<double>(half_);
    twiddle_[j] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
  }

  rtwiddle_.resize(half_ + 1);
  for (std::size_t k = 0; k <= half_; ++k) {
    const double a = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n_);
    rtwiddle_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
  }

  work_.resize(half_);
}

// [RT] FFT phức tại chỗ, kích thước N/2, radix-2 phân thời gian (DIT).
void RealFft::complexFft(std::complex<float>* data) const noexcept {
  const std::size_t m = half_;
  for (std::size_t i = 0; i < m; ++i) {
    const std::size_t j = bitrev_[i];
    if (j > i) std::swap(data[i], data[j]);
  }
  // Mỗi tầng ghép các DFT dài `len/2` thành DFT dài `len`; bước twiddle = m / len.
  for (std::size_t len = 2; len <= m; len <<= 1) {
    const std::size_t halfLen = len >> 1;
    const std::size_t step = m / len;
    for (std::size_t start = 0; start < m; start += len) {
      for (std::size_t k = 0; k < halfLen; ++k) {
        const std::complex<float> w = twiddle_[k * step];
        const std::complex<float> a = data[start + k];
        const std::complex<float> b = data[start + k + halfLen] * w;
        data[start + k] = a + b;
        data[start + k + halfLen] = a - b;
      }
    }
  }
}

// [RT]
void RealFft::forward(const float* in, std::complex<float>* out) noexcept {
  std::complex<float>* z = work_.data();
  for (std::size_t m = 0; m < half_; ++m) z[m] = {in[2 * m], in[2 * m + 1]};
  complexFft(z);

  // Bin 0 và Nyquist: Fe = Re Z[0], Fo = Im Z[0].
  out[0] = {z[0].real() + z[0].imag(), 0.0f};
  out[half_] = {z[0].real() - z[0].imag(), 0.0f};
  for (std::size_t k = 1; k < half_; ++k) {
    const std::complex<float> zk = z[k];
    const std::complex<float> zc = std::conj(z[half_ - k]);
    const std::complex<float> fe = 0.5f * (zk + zc);
    // (zk − zc) / (2i) = −i·(zk − zc)/2
    const std::complex<float> d = 0.5f * (zk - zc);
    const std::complex<float> fo = {d.imag(), -d.real()};
    out[k] = fe + rtwiddle_[k] * fo;
  }
}

// [RT]
void RealFft::inverse(const std::complex<float>* in, float* out) noexcept {
  std::complex<float>* z = work_.data();
  // Dựng lại Z[k] = Fe[k] + i·Fo[k] với Fe = (X[k] + X*[N/2−k])/2,
  // Fo = (X[k] − X*[N/2−k])/2 · W^(−k).
  for (std::size_t k = 0; k < half_; ++k) {
    const std::complex<float> xk = k == 0 ? std::complex<float>(in[0].real(), 0.0f) : in[k];
    const std::complex<float> xc =
        k == 0 ? std::complex<float>(in[half_].real(), 0.0f) : std::conj(in[half_ - k]);
    const std::complex<float> fe = 0.5f * (xk + xc);
    const std::complex<float> fo = 0.5f * (xk - xc) * std::conj(rtwiddle_[k]);
    // Liên hợp trước để dùng lại FFT thuận: IFFT(Z) = conj(FFT(conj(Z))) / M.
    z[k] = std::conj(fe + std::complex<float>(-fo.imag(), fo.real()));
  }
  complexFft(z);
  const float scale = 1.0f / static_cast<float>(half_);
  for (std::size_t m = 0; m < half_; ++m) {
    const std::complex<float> v = std::conj(z[m]) * scale;
    out[2 * m] = v.real();
    out[2 * m + 1] = v.imag();
  }
}

}  // namespace tonecore
