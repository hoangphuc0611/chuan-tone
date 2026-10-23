#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace tonecore {

// Ước lượng tuning của bài: các nốt lệch đều bao nhiêu cent so với lưới La = 440 Hz (beat
// karaoke Việt hay bị dịch tone bằng phần mềm nên lệch lẻ cent).
//
// Thuật toán (ý tưởng của C. Harte, M. Sandler, "Automatic chord identification using a
// quantised chromagram", AES 2005, áp dụng trên STFT thay vì CQT):
//  1. Mỗi khung phổ (phần hoà âm sau HPSS): tìm các đỉnh cục bộ trong dải 400–3000 Hz. Dưới
//     ~400 Hz hai nốt cách nửa cung rơi chung một búp chính của cửa sổ nên đỉnh bị lệch.
//  2. Nội suy parabol trên log biên độ (QIFFT — J. O. Smith, X. Serra, "PARSHL", ICMC 1987)
//     → tần số đỉnh chính xác cỡ 1/50 bin.
//  3. Độ lệch so với nốt gần nhất trong lưới 440 Hz: d ∈ [−50, 50) cent, cộng trọng số = biên độ
//     đỉnh vào histogram VÒNG 100 ô (1 cent/ô; −50 và +50 là cùng một chỗ).
//  4. Làm mượt histogram (Gauss σ = 4 cent, vòng) rồi lấy ĐỈNH (mode), nội suy parabol.
//     Dùng mode thay vì trung bình vì hoạ âm bậc 5 (−14 cent) và bậc 7 (−31 cent) của mọi nốt
//     tạo thành các cụm phụ — trung bình bị kéo lệch, đỉnh chính thì không.
class TuningEstimator {
 public:
  TuningEstimator(double rate, std::size_t fftSize, double minHz = 400.0, double maxHz = 3000.0);

  // Thêm một khung phổ biên độ, fftSize/2 + 1 bin.
  void addFrame(const float* magnitude, std::size_t bins);

  // Cent lệch so với La = 440 Hz, trong [−50, 50). Rỗng nếu quá ít đỉnh (im lặng, chỉ có trống).
  std::optional<float> estimate() const;

  std::size_t peakCount() const noexcept { return peaks_; }

 private:
  static constexpr int kBins = 100;

  double rate_;
  std::size_t fftSize_;
  std::size_t kMin_;
  std::size_t kMax_;
  std::array<double, kBins> hist_{};
  std::size_t peaks_ = 0;
};

}  // namespace tonecore
