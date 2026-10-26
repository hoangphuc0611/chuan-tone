#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "dsp/fft.h"

namespace tonecore {

struct PitchShiftConfig {
  int fftSize = 4096;       // cửa sổ phân tích / tổng hợp (mẫu): 85 ms ở 48 kHz, bin 11,7 Hz
  int synthesisHop = 1024;  // bước tổng hợp (mẫu); fftSize / synthesisHop ≥ 4
  int sincZeros = 16;       // số điểm cắt 0 mỗi bên của nhân sinc khi resample
};

// Đổi cao độ beat n nửa cung, giữ nguyên độ dài và vị trí từng mẫu.
//
// Thuật toán: kéo giãn thời gian bằng phase vocoder với hệ số β = 2^(n/12) rồi đọc lại tín hiệu đã
// giãn nhanh hơn β lần (resample 1/β) → cao độ nhân β, độ dài như cũ.
//  1. Khung phân tích j lấy quanh thời điểm gốc u_j = (j·Hs + N/2)/β − N/2 (làm tròn mẫu), nhân
//     cửa sổ Hann, FFT.
//  2. Pha tổng hợp từng bin tiến theo tần số tức thời đo được giữa hai khung phân tích liên tiếp
//     (Flanagan & Golden 1966; Dolson 1986):
//        Δφ = φ_a(j) − φ_a(j−1) − ω_k·Ha  (quấn về [−π, π]),   ω_tt = ω_k + Δφ / Ha,
//        φ_s(j) = φ_s(j−1) + ω_tt · Hs.
//  3. IFFT, nhân cửa sổ, cộng chồng (OLA) với bước Hs vào tín hiệu đã giãn; chia cho tổng w².
//  4. Mẫu ra m đọc tín hiệu đã giãn tại vị trí β·m bằng nội suy sinc–Kaiser (J. O. Smith, "Digital
//     Audio Resampling"); khi β > 1 hạ tần số cắt xuống 0,5/β để chống alias.
// Tâm khung tổng hợp j·Hs + N/2 ứng với tâm khung phân tích, và mẫu ra m ứng với vị trí β·m trên
// tín hiệu giãn ↔ thời điểm gốc m → bản mới khớp từng mẫu với bản gốc, không có độ trễ (đọc trước
// được vì beat nằm sẵn trong RAM).
//
// Đây là phase vocoder cơ bản (B1, mốc dưới để so sánh). Khoá pha, giữ transient, chung pha
// stereo thêm ở B2/B3.
//
// Cách dùng: start() ở một vị trí bất kỳ của beat, rồi pull() lấy tuần tự từng đoạn. Cấp phát
// trong constructor; start()/pull() không cấp phát. Không an toàn đa luồng.
class PitchShifter {
 public:
  PitchShifter(int channels, double semitones, const PitchShiftConfig& cfg = {});

  int channels() const noexcept { return channels_; }
  double ratio() const noexcept { return beta_; }

  // Bắt đầu render từ frame `begin` của beat `in` (interleaved, `inFrames` frame). `in` phải còn
  // sống tới khi xong. Ngoài [0, inFrames) coi là im lặng.
  void start(const float* in, uint64_t inFrames, uint64_t begin) noexcept;
  // Ghi `frames` frame tiếp theo (interleaved) vào `out`.
  void pull(float* out, uint32_t frames) noexcept;

 private:
  void computeFrame() noexcept;
  float interpolate(const std::vector<float>& ring, double pos) const noexcept;

  int channels_;
  double beta_;
  bool bypass_;
  int n_;
  int hop_;
  int bins_;
  RealFft fft_;
  std::vector<float> window_;
  float olaScale_ = 1.0f;

  // Nhân sinc–Kaiser lấy mẫu sẵn: kernel_[i] = h(i / kTableRes), i ≤ halfWidth·kTableRes.
  std::vector<float> kernel_;
  double halfWidth_ = 0.0;  // nửa độ rộng nhân (mẫu của tín hiệu giãn)

  // Trạng thái theo kênh.
  std::vector<std::vector<float>> prevPhase_;   // pha phân tích của khung trước
  std::vector<std::vector<float>> prevMag_;     // biên độ phân tích của khung trước
  std::vector<std::vector<float>> synthPhase_;  // pha tổng hợp đang tích luỹ
  std::vector<std::vector<float>> ring_;        // tín hiệu đã giãn, vòng tròn theo vị trí tuyệt đối
  uint64_t ringMask_ = 0;

  std::vector<float> frame_;
  std::vector<std::complex<float>> spec_;

  const float* in_ = nullptr;
  uint64_t inFrames_ = 0;
  int64_t nextFrame_ = 0;     // chỉ số khung tổng hợp sẽ tính tiếp
  int64_t prevAnalysis_ = 0;  // vị trí bắt đầu khung phân tích trước
  int64_t completeUpTo_ = 0;  // mẫu giãn < giá trị này đã cộng đủ mọi khung
  int64_t zeroedUpTo_ = 0;    // vị trí giãn < giá trị này đã được xoá về 0 trước khi cộng
  uint64_t outPos_ = 0;       // frame ra tiếp theo (trục thời gian gốc)
};

// Tiện ích offline: đổi cao độ cả tín hiệu (interleaved). Cấp phát.
std::vector<float> pitchShiftBuffer(const float* in, uint64_t frames, int channels,
                                    double semitones, const PitchShiftConfig& cfg = {});

}  // namespace tonecore
