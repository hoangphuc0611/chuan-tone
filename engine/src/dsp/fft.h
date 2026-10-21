#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tonecore {

// FFT cho tín hiệu thực, kích thước N là luỹ thừa của 2 (N ≥ 4). Tự viết.
//
// Thuật toán (Cooley–Tukey 1965; cách "gói" tín hiệu thực theo Oppenheim & Schafer, mục 9.x):
//  1. Gói N mẫu thực thành N/2 số phức z[m] = x[2m] + i·x[2m+1].
//  2. FFT phức radix-2 lặp (đảo bit + các tầng butterfly) kích thước N/2 → Z[k].
//  3. Tách phổ chẵn/lẻ:  Fe[k] = (Z[k] + Z*[N/2−k]) / 2,  Fo[k] = (Z[k] − Z*[N/2−k]) / (2i)
//     rồi ghép  X[k] = Fe[k] + W^k · Fo[k],  W = e^(−2πi/N),  k = 0..N/2.
// Nghịch đảo làm ngược lại các bước trên, đã chia sẵn cho N → inverse(forward(x)) == x.
//
// Cấp phát toàn bộ trong constructor; forward/inverse không cấp phát → dùng được trong audio
// thread. Không an toàn đa luồng (có bộ đệm làm việc bên trong): mỗi luồng dùng một đối tượng.
class RealFft {
 public:
  explicit RealFft(std::size_t n);

  std::size_t size() const noexcept { return n_; }
  // Số bin phổ của forward(): N/2 + 1 (từ 0 tới Nyquist).
  std::size_t bins() const noexcept { return half_ + 1; }

  // [RT] in: N mẫu thực. out: N/2 + 1 số phức.
  void forward(const float* in, std::complex<float>* out) noexcept;

  // [RT] in: N/2 + 1 số phức (phần ảo của bin 0 và bin N/2 bị bỏ qua). out: N mẫu thực.
  void inverse(const std::complex<float>* in, float* out) noexcept;

 private:
  void complexFft(std::complex<float>* data) const noexcept;

  std::size_t n_;
  std::size_t half_;
  std::vector<std::complex<float>> twiddle_;   // e^(−2πi·j/(N/2)), j < N/4 — cho FFT phức N/2
  std::vector<std::complex<float>> rtwiddle_;  // e^(−2πi·k/N),  k ≤ N/2 — cho bước tách phổ
  std::vector<uint32_t> bitrev_;
  std::vector<std::complex<float>> work_;
};

}  // namespace tonecore
