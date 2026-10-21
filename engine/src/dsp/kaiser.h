#pragma once

#include <cmath>

namespace tonecore::dsp {

constexpr double kPi = 3.14159265358979323846;

// Hàm Bessel biến dạng loại 1 bậc 0, khai triển chuỗi I0(x) = Σ ((x/2)^k / k!)² — dùng cho cửa
// sổ Kaiser. Chỉ gọi khi thiết kế bộ lọc (ngoài audio thread).
inline double besselI0(double x) {
  double sum = 1.0;
  double term = 1.0;
  const double half = x * 0.5;
  for (int k = 1; k < 50; ++k) {
    term *= (half / k) * (half / k);
    sum += term;
    if (term < sum * 1e-12) break;
  }
  return sum;
}

// Cửa sổ Kaiser tại u ∈ [−1, 1] (0 ở ngoài). β lớn → dải chặn sâu hơn, dải chuyển rộng hơn.
// Tham khảo: J. F. Kaiser, "Nonrecursive digital filter design using the I0-sinh window
// function", 1974; công thức ước lượng β/độ dài trong Oppenheim & Schafer, mục 7.6.
inline double kaiserWindow(double u, double beta) {
  if (u <= -1.0 || u >= 1.0) return 0.0;
  return besselI0(beta * std::sqrt(1.0 - u * u)) / besselI0(beta);
}

// sinc chuẩn hoá: sin(πx)/(πx).
inline double sinc(double x) {
  if (x == 0.0) return 1.0;
  return std::sin(kPi * x) / (kPi * x);
}

}  // namespace tonecore::dsp
