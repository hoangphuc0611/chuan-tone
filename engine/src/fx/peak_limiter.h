#pragma once

#include <cstdint>
#include <vector>

namespace tonecore {

// Limiter đỉnh nhìn trước cho beat đã đổi tone (M4).
//
// Đổi pha (phase vocoder) làm hình sóng đổi → đỉnh có thể vượt 0 dBFS khi beat gốc đã master sát
// 0 dBFS (đo trên beat dev: tới +3,9 dB, hàng nghìn mẫu / 15 s ở +4) → kẹp ở tầng ra nghe thành
// méo. Render chạy ở luồng nền nên được nhìn trước thoải mái:
//   r(n)   = min(1, trần / max_kênh |x(n)|)            — hệ số cần để mẫu n không vượt trần
//   g*(n)  = min r(k), k ∈ [n, n + L]                  — bắt đầu giảm L mẫu trước đỉnh
//   g(n)   = g(n−1) + (g*(n) − g(n−1))·a,  a = a_attack nếu giảm, a_release nếu tăng
// a_attack đủ nhanh để sau L mẫu đã giảm > 99% (đỉnh vượt trần < 1%, trần 0,98 vẫn < 1,0); release
// mũ τ = 60 ms. Đỉnh ngắn và thưa nên gần như không nghe ra. Một hệ số chung cho mọi kênh (giữ ảnh
// stereo). Dưới trần thì đi thẳng (hệ số 1, đúng từng mẫu). Kẹp ±1 cuối cùng để chắc chắn.
//
// Trễ L mẫu: process() nhận khối mới, trả khối đã giới hạn chậm L mẫu. Dùng cho beat đổi tone (M4,
// nhìn trước 2 ms) và limiter đầu ra của engine (M5, 1 ms).
class PeakLimiter {
 public:
  PeakLimiter(int channels, double sampleRate, float ceiling = 0.98f, double lookaheadMs = 2.0,
              double releaseMs = 60.0);

  int lookahead() const noexcept { return lookahead_; }
  void reset() noexcept;
  // Giới hạn `frames` frame (interleaved) vào `out`, trong đó `in` là `frames` frame MỚI; đầu ra là
  // `frames` frame chậm `lookahead()` so với đầu vào. Cấp phát trong constructor; hàm này không.
  void process(const float* in, float* out, uint32_t frames) noexcept;

 private:
  int channels_;
  int lookahead_;
  float ceiling_;
  float attack_;
  float release_;
  float gain_ = 1.0f;
  std::vector<float> delay_;  // (lookahead + 1) frame gần nhất, vòng tròn
  uint32_t head_ = 0;
  // Cực tiểu cửa sổ trượt của r(n) trên L + 1 mẫu gần nhất: hàng đợi đơn điệu (giá trị tăng dần từ
  // đầu tới cuối) — mỗi mẫu vào / ra hàng đợi đúng một lần → O(1) trung bình thay vì quét L + 1.
  std::vector<float> minVal_;
  std::vector<uint64_t> minIdx_;
  uint32_t minHead_ = 0;  // phần tử đầu (nhỏ nhất)
  uint32_t minSize_ = 0;
  uint64_t count_ = 0;  // số mẫu đã vào
};

// Tiện ích offline: giới hạn cả tín hiệu tại chỗ (không trễ).
void limitPeaks(std::vector<float>& interleaved, int channels, double sampleRate,
                float ceiling = 0.98f);

}  // namespace tonecore
