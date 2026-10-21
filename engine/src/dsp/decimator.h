#pragma once

#include <cstddef>
#include <vector>

namespace tonecore {

// Hạ tần số lấy mẫu theo hệ số nguyên M (vd. 48 kHz → 16 kHz, M = 3) cho bộ dò cao độ.
//
// Lọc thông thấp FIR pha tuyến tính (sinc cửa sổ Kaiser) rồi chỉ giữ 1 trong M mẫu. Dạng
// polyphase: chỉ tính tích chập tại các mẫu được giữ → tốn N/M phép nhân mỗi mẫu đầu vào.
// Độ trễ nhóm = (N − 1) / 2 mẫu đầu vào.
//
// Mặc định: tần số cắt 0,42 × tần số mới (≈ 6,7 kHz khi ra 16 kHz), β = 6 (dải chặn ≈ −60 dB),
// N = 96 → trễ ≈ 1 ms ở 48 kHz. Đủ cho dò cao độ giọng hát (năng lượng quan trọng < 5 kHz).
class Decimator {
 public:
  Decimator(int factor, int taps = 96, double cutoffOfOutputRate = 0.42, double beta = 6.0);

  int factor() const noexcept { return factor_; }
  int taps() const noexcept { return static_cast<int>(h_.size()); }
  // Độ trễ nhóm tính theo mẫu đầu vào.
  double groupDelayInput() const noexcept { return (static_cast<double>(h_.size()) - 1.0) / 2.0; }

  void reset() noexcept;

  // [RT] Nhận `n` mẫu, ghi tối đa n / M + 1 mẫu vào `out`. Trả về số mẫu đã ghi.
  std::size_t process(const float* in, std::size_t n, float* out) noexcept;

  const std::vector<float>& coefficients() const noexcept { return h_; }

 private:
  int factor_;
  std::vector<float> h_;
  // Lịch sử nhân đôi: mỗi mẫu ghi ở pos và pos + N → luôn đọc được N mẫu liên tiếp, không cần
  // xử lý vòng.
  std::vector<float> hist_;
  std::size_t pos_ = 0;
  int phase_ = 0;
};

}  // namespace tonecore
