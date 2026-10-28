#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace tonecore {

enum class SvfType : int32_t {
  Off = 0,
  LowPass = 1,
  HighPass = 2,
  Bell = 3,
  LowShelf = 4,
  HighShelf = 5
};

// Hệ số của bộ lọc biến trạng thái (SVF) dạng TPT — xem lớp Svf.
//
// Nguyên mẫu analog chuẩn hoá (tần số cắt 1, độ tắt k = 1/Q):
//   D(s) = s² + k·s + 1,   LP = 1/D,   BP = s/D,   HP = s²/D,   x = HP + k·BP + LP.
// Mọi đáp ứng là tổ hợp H = m0·x + m1·BP + m2·LP (tự suy ra từ nguyên mẫu analog trong RBJ Audio EQ
// Cookbook; cùng dạng với Simper, "Linear Trap Integrated SVF", Cytomic 2013), A = 10^(dB/40):
//   - Bell:       k = 1/(Q·A),                 m = (1, k·(A² − 1), 0)
//       ⇔ (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1)
//   - Low shelf:  cắt ở ω0/√A (g = tan(ω0/2)/√A), k = 1/Q,  m = (1, k·(A − 1), A² − 1)
//       ⇔ A·(s² + (√A/Q)·s + A) / (A·s² + (√A/Q)·s + 1)
//   - High shelf: cắt ở ω0·√A (g = tan(ω0/2)·√A), k = 1/Q, m = (A², k·A·(1 − A), 1 − A²)
//       ⇔ A·(A·s² + (√A/Q)·s + 1) / (s² + (√A/Q)·s + A)
// Rời rạc hoá bằng biến đổi song tuyến tính có prewarp tại ω0 (g = tan(π·f0/fs)) — đúng như RBJ,
// nên đáp ứng biên độ trùng công thức cookbook (kiểm trong test). Khác biquad: trạng thái là "điện
// áp tụ" của hai tích phân hình thang → ổn định và không nổ khi thông số đổi liên tục (Zavalishin,
// "The Art of VA Filter Design", ch. 3–4).
struct SvfCoeffs {
  float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;  // từ g, k (xem Svf::process)
  float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
  double g = 0.0;
  double k = 1.0;

  static SvfCoeffs make(SvfType type, double sampleRate, double f0, double q,
                        double gainDb) noexcept {
    SvfCoeffs c;
    const double f = std::clamp(f0, 1.0, 0.49 * sampleRate);
    const double w = std::tan(3.14159265358979323846 * f / sampleRate);
    const double qq = std::max(q, 0.05);
    const double a = std::pow(10.0, gainDb / 40.0);
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    c.g = w;
    c.k = 1.0 / qq;
    switch (type) {
      case SvfType::Off:
        break;
      case SvfType::LowPass:
        m0 = 0.0;
        m2 = 1.0;
        break;
      case SvfType::HighPass:
        m1 = -c.k;
        m2 = -1.0;
        break;
      case SvfType::Bell:
        c.k = 1.0 / (qq * a);
        m1 = c.k * (a * a - 1.0);
        break;
      case SvfType::LowShelf:
        c.g = w / std::sqrt(a);
        m1 = c.k * (a - 1.0);
        m2 = a * a - 1.0;
        break;
      case SvfType::HighShelf:
        c.g = w * std::sqrt(a);
        m0 = a * a;
        m1 = c.k * a * (1.0 - a);
        m2 = 1.0 - a * a;
        break;
    }
    const double a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a1 = static_cast<float>(a1);
    c.a2 = static_cast<float>(c.g * a1);
    c.a3 = static_cast<float>(c.g * c.g * a1);
    c.m0 = static_cast<float>(m0);
    c.m1 = static_cast<float>(m1);
    c.m2 = static_cast<float>(m2);
    return c;
  }

  // |H(e^jω)| tại f (Hz) — đáp ứng đúng của bộ lọc số: s = j·tan(π·f/fs)/g thay vào nguyên mẫu.
  double magnitudeAt(double f, double sampleRate) const noexcept {
    const std::complex<double> s(0.0, std::tan(3.14159265358979323846 * f / sampleRate) / g);
    const std::complex<double> d = s * s + k * s + 1.0;
    return std::abs(static_cast<double>(m0) + static_cast<double>(m1) * s / d +
                    static_cast<double>(m2) / d);
  }
};

// SVF dạng TPT (hai tích phân hình thang, giải vòng phản hồi không trễ — Zavalishin ch. 3):
//   v3 = x − ic2,  v1 = a1·ic1 + a2·v3 (băng),  v2 = ic2 + a2·ic1 + a3·v3 (thấp),
//   ic1 ← 2·v1 − ic1,  ic2 ← 2·v2 − ic2,   y = m0·x + m1·v1 + m2·v2.
class Svf {
 public:
  void reset() noexcept {
    ic1_ = 0.0f;
    ic2_ = 0.0f;
  }

  // [RT]
  float process(float x, const SvfCoeffs& c) noexcept {
    const float v3 = x - ic2_;
    const float v1 = c.a1 * ic1_ + c.a2 * v3;
    const float v2 = ic2_ + c.a2 * ic1_ + c.a3 * v3;
    ic1_ = 2.0f * v1 - ic1_;
    ic2_ = 2.0f * v2 - ic2_;
    return c.m0 * x + c.m1 * v1 + c.m2 * v2;
  }

  // [RT] Cả khối tại chỗ. Hệ số và trạng thái chép ra biến cục bộ: ghi qua `x` không thể đè lên
  // chúng nên trình biên dịch giữ trong thanh ghi (gọi process() từng mẫu thì phải đọc / ghi lại
  // bộ nhớ mỗi mẫu — chậm ~3 lần).
  void processBlock(float* x, uint32_t n, const SvfCoeffs& c) noexcept {
    const float a1 = c.a1, a2 = c.a2, a3 = c.a3, m0 = c.m0, m1 = c.m1, m2 = c.m2;
    float ic1 = ic1_, ic2 = ic2_;
    for (uint32_t i = 0; i < n; ++i) {
      const float v0 = x[i];
      const float v3 = v0 - ic2;
      const float v1 = a1 * ic1 + a2 * v3;
      const float v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2.0f * v1 - ic1;
      ic2 = 2.0f * v2 - ic2;
      x[i] = m0 * v0 + m1 * v1 + m2 * v2;
    }
    ic1_ = ic1;
    ic2_ = ic2;
  }

 private:
  float ic1_ = 0.0f;
  float ic2_ = 0.0f;
};

}  // namespace tonecore
