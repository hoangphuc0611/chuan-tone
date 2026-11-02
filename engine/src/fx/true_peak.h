#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "fx/peak_limiter.h"

namespace tonecore {

// Đỉnh thật (true-peak, ITU-R BS.1770-4 phụ lục 2): đỉnh của tín hiệu liên tục mà các mẫu biểu
// diễn — có thể cao hơn đỉnh mẫu tới ~3 dB (vd. sin fs/4 lệch pha 45°: mẫu ±0,707, đỉnh thật 1).
// Bộ mã hoá AAC / bộ chuyển DA tái tạo phần giữa các mẫu nên file xuất cần giữ đỉnh thật dưới trần.
//
// Ước lượng: lấy mẫu ×4 — 3 điểm xen giữa mỗi cặp mẫu bằng nội suy sinc có cửa sổ Kaiser (β = 8)
// 16 hệ số mỗi pha (đủ chính xác < 0,1 dB tới ~20 kHz), cộng chính mẫu gốc; đỉnh = max |giá trị|
// trên mọi kênh. Hệ số tự tính (không dùng bảng 48 hệ số của chuẩn).
class TruePeakDetector {
 public:
  static constexpr int kTaps = 16;
  static constexpr int kDelay = kTaps / 2;  // đỉnh của frame n có khi đưa vào frame n + kDelay

  explicit TruePeakDetector(int channels);
  void reset() noexcept;
  // `in`: frames frame mới (interleaved). peaks[f]: đỉnh thật của frame đã vào trước đó kDelay
  // frame (gồm khoảng từ frame đó tới frame kế). Không cấp phát.
  void process(const float* in, float* peaks, uint32_t frames) noexcept;

 private:
  int channels_;
  std::array<std::array<float, kTaps>, 3> phase_{};  // pha 1/4, 2/4, 3/4
  std::vector<float> hist_;                          // kTaps frame gần nhất, vòng tròn
  uint32_t head_ = 0;
};

// Đỉnh thật (tuyến tính) của cả tín hiệu — đo / kiểm tra file xuất.
float measureTruePeak(const float* interleaved, std::size_t frames, int channels);

// Limiter giữ đỉnh thật dưới `ceiling` (tuyến tính): PeakLimiter dùng đỉnh thật làm tín hiệu
// điều khiển. Trễ = kDelay + lookahead của PeakLimiter. Chạy offline (render bản thu).
class TruePeakLimiter {
 public:
  TruePeakLimiter(int channels, double sampleRate, float ceiling, double lookaheadMs = 1.5,
                  double releaseMs = 80.0);
  int latency() const noexcept { return TruePeakDetector::kDelay + limiter_.lookahead(); }
  // Như PeakLimiter::process: `frames` frame mới vào, `frames` frame ra chậm latency().
  void process(const float* in, float* out, uint32_t frames);

 private:
  int channels_;
  TruePeakDetector detector_;
  PeakLimiter limiter_;
  std::vector<float> delay_;  // kDelay frame để mẫu khớp với đỉnh đã dò
  uint32_t head_ = 0;
  std::vector<float> peaks_;
  std::vector<float> aligned_;
};

}  // namespace tonecore
