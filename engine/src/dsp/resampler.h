#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tonecore {

// Đổi tần số lấy mẫu offline (vd. beat 44,1 kHz → 48 kHz khi nạp bài). Không dùng trong audio
// thread (có cấp phát).
//
// Thuật toán: nội suy band-limited bằng sinc có cửa sổ Kaiser (J. O. Smith, "Digital Audio
// Resampling Home Page"). Với mỗi mẫu đầu ra tại vị trí t (tính theo mẫu đầu vào), cộng các mẫu
// đầu vào lân cận nhân với nhân lọc h(t − n):
//
//     h(x) = 2·fc · sinc(2·fc·x) · kaiser(x / L),   L = Z / (2·fc)
//
//  - fc: tần số cắt (chu kỳ/mẫu đầu vào) = 0,5 · min(1, đích/nguồn) · rolloff — khi hạ tần số
//    lấy mẫu thì hạ cả tần số cắt để chống alias.
//  - Z: số điểm cắt 0 mỗi bên (độ dài bộ lọc). Kaiser β = 8 → dải chặn ~ −80 dB.
//  - h được lấy mẫu trước vào bảng (512 điểm mỗi mẫu đầu vào) và nội suy tuyến tính.
class Resampler {
 public:
  Resampler(double srcRate, double dstRate);

  // `in`: interleaved, `frames` frame, `channels` kênh. Trả về interleaved ở tần số đích.
  std::vector<float> process(const float* in, std::size_t frames, int channels) const;

  // Như trên cho PCM 16-bit (dùng khi nạp beat để tiết kiệm RAM: bài 4 phút stereo 48 kHz
  // chỉ ~46 MB thay vì ~92 MB nếu dùng float). Tính toán nội bộ bằng float, làm tròn + kẹp.
  std::vector<int16_t> process(const int16_t* in, std::size_t frames, int channels) const;

  // Số frame đầu ra cho `frames` frame đầu vào.
  std::size_t outputFrames(std::size_t frames) const;

  double ratio() const noexcept { return dstRate_ / srcRate_; }

 private:
  float kernel(double x) const noexcept;
  template <typename T>
  std::vector<T> processImpl(const T* in, std::size_t frames, int channels) const;

  double srcRate_;
  double dstRate_;
  double halfWidth_;  // L, tính theo mẫu đầu vào
  std::vector<float> table_;
};

}  // namespace tonecore
