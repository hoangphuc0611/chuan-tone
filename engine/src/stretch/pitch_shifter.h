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
  // true: khoá pha identity + chung pha các kênh (B2, mặc định). false: phase vocoder cơ bản, từng
  // kênh riêng (B1 — chỉ giữ làm mốc so sánh trong beatlab).
  bool phaseLock = true;
};

// Đổi cao độ beat n nửa cung, giữ nguyên độ dài và vị trí từng mẫu.
//
// Thuật toán: kéo giãn thời gian bằng phase vocoder với hệ số β = 2^(n/12) rồi đọc lại tín hiệu đã
// giãn nhanh hơn β lần (resample 1/β) → cao độ nhân β, độ dài như cũ.
//  1. Khung phân tích j lấy quanh thời điểm gốc u_j = (j·Hs + N/2)/β − N/2 (làm tròn mẫu), nhân
//     cửa sổ Hann, FFT từng kênh.
//  2. Khoá pha identity (Laroche & Dolson 1999), tính trên phổ tham chiếu L + R:
//     - Đỉnh phổ p (lớn hơn 2 bin mỗi bên) tiến pha theo tần số tức thời đo giữa hai khung phân
//     tích
//       (Flanagan & Golden 1966; Dolson 1986):
//          Δ = arg(X_j[p]·X*_{j−1}[p]) − ω_p·Ha  (quấn về [−π, π]),   ω_tt = ω_p + Δ / Ha,
//          φ_s(p) = φ_s,trước(p) + ω_tt·Hs.
//     - Mọi bin trong "vùng" của đỉnh (tới điểm thấp nhất giữa hai đỉnh) xoay cùng góc với đỉnh:
//          φ_s(k) = φ_s(p) + φ_a(k) − φ_a(p)  ⇔  Y[k] = X[k]·e^{iθ_p},  θ_p = φ_s(p) − φ_a(p).
//       Quan hệ pha giữa các bin của một nốt (quyết định vị trí và hình dạng của âm trong khung)
//       được lấy lại từ phổ phân tích ở MỌI khung — phase vocoder cơ bản để quan hệ này trôi (bị
//       nhân β) → tiếng "phơ", nhỏ tiếng và lệch thời gian (docs/stretch-results.md, B1).
//     - Cùng góc θ áp cho mọi kênh → chênh pha / biên độ giữa L và R giữ nguyên từng bin (ảnh
//       stereo giữ, beat mono ra vẫn mono). Chỉ tính atan2 / sincos cho các đỉnh.
//     - Khung trước im lặng (> 100 dB thấp hơn) → θ = 0 (lấy nguyên pha phân tích).
//  3. IFFT, nhân cửa sổ, cộng chồng (OLA) với bước Hs vào tín hiệu đã giãn; chia cho tổng w².
//  4. Mẫu ra m đọc tín hiệu đã giãn tại vị trí β·m bằng nội suy sinc–Kaiser (J. O. Smith, "Digital
//     Audio Resampling"); khi β > 1 hạ tần số cắt xuống 0,5/β để chống alias. Trọng số tính một lần
//     cho mọi kênh.
// Tâm khung tổng hợp j·Hs + N/2 ứng với tâm khung phân tích, và mẫu ra m ứng với vị trí β·m trên
// tín hiệu giãn ↔ thời điểm gốc m → bản mới khớp từng mẫu với bản gốc, không có độ trễ (đọc trước
// được vì beat nằm sẵn trong RAM). Giữ transient (trống) thêm ở B3.
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
  void analyze(int64_t analysis) noexcept;
  void lockPhases(float hopA, float frameMax) noexcept;
  void propagateBasic(float hopA) noexcept;
  void synthesize(int64_t synthStart) noexcept;
  void interpolate(double pos, float* out) noexcept;

  int channels_;
  double beta_;
  bool bypass_;
  bool phaseLock_;
  int n_;
  int hop_;
  int bins_;
  RealFft fft_;
  std::vector<float> window_;
  float olaScale_ = 1.0f;

  // Nhân sinc–Kaiser đa pha: (kPhases + 1) hàng × taps_ cột (xem constructor).
  std::vector<float> poly_;
  std::vector<float> weights_;  // trọng số nội suy của mẫu ra hiện tại
  int halfTaps_ = 0;            // K = ⌈nửa độ rộng nhân⌉
  int taps_ = 0;                // 2K

  // Theo kênh.
  std::vector<std::vector<std::complex<float>>> spec_;  // phổ phân tích khung hiện tại
  std::vector<std::vector<float>> ring_;  // tín hiệu đã giãn, vòng tròn theo vị trí tuyệt đối
  uint64_t ringMask_ = 0;
  std::size_t ringCap_ = 0;  // số ô chính của vòng đệm (luỹ thừa 2); sau đó là taps_ ô gương

  // Khoá pha (chung mọi kênh).
  std::vector<std::complex<float>> ref_;      // phổ tham chiếu khung hiện tại (L + R)
  std::vector<std::complex<float>> prevRef_;  // … khung trước
  std::vector<float> power_;                  // Σ |X_c|² từng bin
  std::vector<float> rot_;                    // góc xoay θ từng bin (khung trước → khung này)
  std::vector<std::complex<float>> unit_;     // e^{iθ} từng bin
  std::vector<int> peaks_;
  std::vector<float> peakTheta_;
  float prevMax_ = 0.0f;  // đỉnh công suất khung trước

  // Phase vocoder cơ bản (phaseLock = false), theo kênh.
  std::vector<std::vector<float>> prevPhase_;
  std::vector<std::vector<float>> prevMag_;
  std::vector<std::vector<float>> synthPhase_;

  std::vector<float> frame_;

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
