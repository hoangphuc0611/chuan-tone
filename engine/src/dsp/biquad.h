#pragma once

#include <cmath>
#include <complex>

namespace tonecore {

// Hệ số bộ lọc bậc 2 đã chuẩn hoá a0 = 1:
//   y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]
struct BiquadCoeffs {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

  // Công thức theo R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter
  // coefficients" (W3C Audio EQ Cookbook). f0: tần số cắt (Hz), q: hệ số phẩm chất.
  static BiquadCoeffs highpass(double sampleRate, double f0, double q = 0.70710678118654752) {
    const double w0 = 2.0 * 3.14159265358979323846 * f0 / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = (1.0 + cosw) / 2.0 / a0;
    c.b1 = -(1.0 + cosw) / a0;
    c.b2 = c.b0;
    c.a1 = -2.0 * cosw / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
  }

  static BiquadCoeffs lowpass(double sampleRate, double f0, double q = 0.70710678118654752) {
    const double w0 = 2.0 * 3.14159265358979323846 * f0 / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = (1.0 - cosw) / 2.0 / a0;
    c.b1 = (1.0 - cosw) / a0;
    c.b2 = c.b0;
    c.a1 = -2.0 * cosw / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
  }

  // Đáp ứng biên độ |H(e^jω)| tại tần số f (Hz) — dùng cho test/hiển thị.
  double magnitudeAt(double f, double sampleRate) const {
    const double w = 2.0 * 3.14159265358979323846 * f / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
  }
};

// Bộ lọc biquad dạng Direct Form II chuyển vị. Trạng thái bằng double để tránh nhiễu làm tròn ở
// tần số cắt thấp (vd. HPF 80 Hz ở 48 kHz có cực rất gần vòng tròn đơn vị).
class Biquad {
 public:
  void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
  void reset() noexcept { s1_ = s2_ = 0.0; }

  // Đặt trạng thái như thể đầu vào đã đứng yên ở `x` từ lâu (đầu ra = x·độ lợi DC). Dùng khi bắt
  // đầu một đoạn mới để bộ lọc không phải "chạy lên" từ 0.
  void resetSteady(float x) noexcept {
    const auto v = static_cast<double>(x);
    s2_ = (c_.b2 - c_.a2 * dcGain()) * v;
    s1_ = (c_.b1 - c_.a1 * dcGain()) * v + s2_;
  }

  double dcGain() const noexcept { return (c_.b0 + c_.b1 + c_.b2) / (1.0 + c_.a1 + c_.a2); }

  // [RT]
  float process(float x) noexcept {
    const auto in = static_cast<double>(x);
    const double y = c_.b0 * in + s1_;
    s1_ = c_.b1 * in - c_.a1 * y + s2_;
    s2_ = c_.b2 * in - c_.a2 * y;
    return static_cast<float>(y);
  }

  // [RT] Xử lý tại chỗ hoặc in → out (có thể trùng nhau).
  void process(const float* in, float* out, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) out[i] = process(in[i]);
  }

 private:
  BiquadCoeffs c_;
  double s1_ = 0.0;
  double s2_ = 0.0;
};

}  // namespace tonecore
