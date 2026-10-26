#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "dsp/fft.h"

namespace tonecore {

// Dò onset (tiếng đánh) trên beat nằm sẵn trong RAM, chạy trước vị trí đang cần — dùng để giữ
// transient khi đổi tone (PitchShifter, B3).
//
// Hàm dò: spectral flux trên log biên độ (Bello et al. 2005; Dixon 2006) của tín hiệu trung bình
// các kênh, khung Hann ~21 ms, bước 1/4 khung (~5 ms):
//     F(t) = (1/K) Σ_k max(0, log(1 + γ|X_t[k]|) − log(1 + γ|X_{t−1}[k]|)),   k từ 30 Hz.
// Onset = đỉnh cục bộ của F trong ±30 ms và F > 1,5·trung vị(±100 ms) + δ. Thời điểm tinh chỉnh
// trong miền thời gian: điểm mà năng lượng 2 ms sau / năng lượng 10 ms trước (của sai phân bậc 1 —
// nhấn cao tần) lớn nhất, trong cửa sổ quanh khung có đỉnh.
//
// Với mỗi onset còn tính mức "trội" g_k ∈ [0, 1] từng bin (lưới bin của FFT `binsFftSize`): công
// suất 2048 mẫu ngay sau onset so với 2048 mẫu ngay trước (cách onset 3 ms), tăng ≤ 3 dB → 0, ≥ 9
// dB → 1. Bin trội = năng lượng của tiếng đánh chiếm phần lớn; bin không trội = nền (hợp âm, pad)
// vẫn ngân.
//
// Dò lười: ensure(pos) chạy dò tới khi mọi onset < pos đã được quyết định. Cấp phát trong
// constructor; start() / ensure() không cấp phát.
class TransientDetector {
 public:
  struct Onset {
    double time = 0.0;      // mẫu (trục thời gian gốc), có phần lẻ
    std::vector<float> g;   // mức trội từng bin, binsFftSize/2 + 1 phần tử
    float strength = 0.0f;  // phần năng lượng mới (nhấn cao tần) — xem kMinNewEnergy
  };

  TransientDetector(int channels, double sampleRate, int binsFftSize);

  void start(const float* in, uint64_t inFrames, int64_t from) noexcept;
  // Dò tới khi chắc chắn đã có mọi onset có thời điểm < `pos`.
  void ensure(int64_t pos) noexcept;
  // Các onset đã dò, thời điểm tăng dần. Onset cũ hơn `before` có thể bị bỏ để giữ chỗ.
  std::size_t count() const noexcept { return count_; }
  const Onset& onset(std::size_t i) const noexcept { return onsets_[(head_ + i) % onsets_.size()]; }
  void dropBefore(double before) noexcept;

 private:
  void computeFlux() noexcept;
  void decide(int64_t frame) noexcept;
  double refine(int64_t frame) const noexcept;
  void classify(Onset& o) noexcept;
  float sample(int64_t t) const noexcept;  // trung bình các kênh, 0 ngoài bài

  int channels_;
  double rate_;
  int frame_;  // độ dài khung dò (luỹ thừa 2)
  int hop_;
  int bins_;  // số bin dùng để tính flux
  int kMin_;
  int median_;  // nửa cửa sổ trung vị (khung)
  int peak_;    // nửa cửa sổ đỉnh cục bộ (khung)
  RealFft fft_;
  std::vector<float> window_;
  std::vector<float> buf_;
  std::vector<std::complex<float>> spec_;
  std::vector<float> prevLog_;
  std::vector<float> flux_;  // vòng tròn theo chỉ số khung
  std::vector<float> scratch_;

  // Phân loại bin trội.
  int clsSize_;
  RealFft clsFft_;
  std::vector<float> clsWindow_;  // Tukey dốc nhanh ở đầu (xem tukeyFromOnset)
  std::vector<float> clsBuf_;
  std::vector<std::complex<float>> clsSpec_;
  std::vector<float> prePow_;
  std::vector<float> postPow_;

  std::vector<Onset> onsets_;  // vòng tròn
  std::size_t head_ = 0;
  std::size_t count_ = 0;

  const float* in_ = nullptr;
  uint64_t inFrames_ = 0;
  int64_t firstFrame_ = 0;   // khung dò đầu tiên của lần start() này
  int64_t nextFrame_ = 0;    // khung sẽ tính flux tiếp
  int64_t decidedUpTo_ = 0;  // các khung < giá trị này đã quyết định có / không có onset
};

}  // namespace tonecore
