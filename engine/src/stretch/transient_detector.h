#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "dsp/fft.h"
#include "stretch/input_view.h"

namespace tonecore {

// Dò onset (tiếng đánh) trên beat nằm sẵn trong RAM, chạy trước vị trí đang cần — dùng để giữ
// transient khi đổi tone (PitchShifter, bước 2b).
//
// Hàm dò (phân giải 1 ms, để bắt cả tiếng đánh sát nhau và tiếng nằm trên đuôi tiếng trước — bản
// spectral flux khung 21 ms trước đây bỏ sót chúng trên beat thật, làm các tiếng đó bị "neo" sai
// nhịp; xem docs/stretch-results.md, B4):
//   d(t) = m(t) − m(t−1)  (m = trung bình các kênh; sai phân nhấn cao tần — tiếng đánh nằm ở đó)
//   E_b  = năng lượng trung bình của d trong khối 1 ms thứ b
//   R_b  = 10·log10(trung bình E_b, E_{b+1}) − 10·log10(trung bình E_{b−10…b−3})  (dB)
// Onset = R_b ≥ 8 dB, là đỉnh của R trong ±5 ms, mức E ≥ −30 dB so với đỉnh gần đây (bỏ nhiễu nền
// lúc gần im lặng), cách onset trước ≥ 10 ms. Thời điểm tinh chỉnh tới từng mẫu: điểm có năng lượng
// 1 ms sau / 5 ms trước lớn nhất trong ±3 ms.
//
// Với mỗi onset còn tính mức "trội" g_k ∈ [0, 1] từng bin (lưới bin của FFT `binsFftSize`): công
// suất ~21 ms ngay sau onset so với ~21 ms ngay trước (cách onset 3 ms), tăng ≤ 3 dB → 0, ≥ 9 dB →
// 1. Bin trội = năng lượng của tiếng đánh chiếm phần lớn; bin không trội = nền (hợp âm, pad) vẫn
// ngân.
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

  void start(const InputView& in, int64_t from) noexcept;
  void start(const float* in, uint64_t inFrames, int64_t from) noexcept {
    start(InputView{in, nullptr, inFrames, channels_}, from);
  }
  // Dò tới khi chắc chắn đã có mọi onset có thời điểm < `pos`.
  void ensure(int64_t pos) noexcept;
  // Các onset đã dò, thời điểm tăng dần. Onset cũ hơn `before` có thể bị bỏ để giữ chỗ.
  std::size_t count() const noexcept { return count_; }
  const Onset& onset(std::size_t i) const noexcept { return onsets_[(head_ + i) % onsets_.size()]; }
  void dropBefore(double before) noexcept;

 private:
  void computeBlock() noexcept;
  void decide(int64_t block) noexcept;
  double refine(int64_t block) const noexcept;
  void classify(Onset& o) noexcept;
  float sample(int64_t t) const noexcept;  // trung bình các kênh, 0 ngoài bài
  float rise(int64_t block) const noexcept;

  int channels_;
  double rate_;
  int block_;                  // mẫu / khối (~1 ms)
  std::vector<float> energy_;  // E_b, vòng tròn theo chỉ số khối
  float peakTrack_ = 0.0f;     // đỉnh E gần đây (tắt dần ~1 s)
  float peakDecay_ = 1.0f;

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

  InputView in_;
  int64_t firstBlock_ = 0;   // khối đầu tiên của lần start() này
  int64_t nextBlock_ = 0;    // khối sẽ tính năng lượng tiếp
  int64_t decidedUpTo_ = 0;  // các khối < giá trị này đã quyết định có / không có onset
};

}  // namespace tonecore
