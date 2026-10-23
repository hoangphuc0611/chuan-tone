#include "key/hpss.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "dsp/fft.h"

namespace tonecore {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Cửa sổ trượt luôn giữ các giá trị đã sắp xếp (tối đa vài chục phần tử): chèn/xoá bằng dịch
// mảng O(n), trung vị đọc trực tiếp. Rẻ hơn gọi nth_element lại cho mỗi bin mỗi khung vì mỗi bước
// chỉ đổi 1 giá trị.
class SortedWindow {
 public:
  explicit SortedWindow(float* storage) : v_(storage) {}
  void clear() { n_ = 0; }
  void insert(float x) {
    std::size_t i = n_;
    while (i > 0 && v_[i - 1] > x) {
      v_[i] = v_[i - 1];
      --i;
    }
    v_[i] = x;
    ++n_;
  }
  // Xoá một phần tử bằng đúng x (x chắc chắn đã được chèn trước đó).
  void erase(float x) {
    std::size_t i = 0;
    while (i < n_ && v_[i] != x) ++i;
    if (i == n_) return;
    for (; i + 1 < n_; ++i) v_[i] = v_[i + 1];
    --n_;
  }
  float median() const { return v_[n_ / 2]; }

 private:
  float* v_;
  std::size_t n_ = 0;
};

bool validConfig(const HpssConfig& c) {
  const bool pow2 = c.fftSize >= 16 && (c.fftSize & (c.fftSize - 1)) == 0;
  return pow2 && c.hop > 0 && c.hop <= c.fftSize / 2 && c.fftSize % c.hop == 0 &&
         c.timeMedian >= 1 && c.timeMedian % 2 == 1 && c.freqMedian >= 1 && c.freqMedian % 2 == 1 &&
         c.maskPower > 0.0f;
}

}  // namespace

tc_result separateHarmonicPercussive(const std::vector<float>& x, double rate,
                                     const HpssConfig& cfg, const HarmonicFrameFn& onHarmonicFrame,
                                     const ProgressFn& onProgress, HpssOutput& out) {
  out = HpssOutput{};
  if (!validConfig(cfg) || !(rate > 0.0)) return TC_ERR_INVALID_ARG;
  const std::size_t n = x.size();
  out.harmonic.assign(n, 0.0f);
  out.percussive.assign(n, 0.0f);
  if (n == 0) return TC_OK;

  const std::size_t N = cfg.fftSize;
  const std::size_t H = cfg.hop;
  const std::size_t bins = N / 2 + 1;
  const auto L = static_cast<std::size_t>(cfg.timeMedian);
  const std::size_t r = (L - 1) / 2;
  const auto rp = static_cast<std::size_t>((cfg.freqMedian - 1) / 2);

  // Cửa sổ Hann tuần hoàn dùng cho cả phân tích và tổng hợp. Tổng chồng Σ w²[i − jH] tuần hoàn
  // chu kỳ H (hằng số với Hann khi H = N/4) → chia cho nó để tổng hợp lại đúng tín hiệu gốc.
  std::vector<float> w(N);
  for (std::size_t i = 0; i < N; ++i) {
    w[i] = static_cast<float>(
        0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(N)));
  }
  std::vector<float> olaNorm(H, 0.0f);
  for (std::size_t i = 0; i < N; ++i) olaNorm[i % H] += w[i] * w[i];

  // Đệm N − H số 0 ở đầu để mẫu gốc đầu tiên đã được đủ N/H khung phủ. Khung j bắt đầu tại
  // j·H − pad (toạ độ gốc). Số khung J: đủ để mẫu cuối n − 1 cũng được phủ đủ.
  const std::size_t pad = N - H;
  const std::size_t J = (n - 1 + pad) / H + 1;

  RealFft fft(N);
  std::vector<std::complex<float>> specRing(L * bins);
  std::vector<float> magRing(L * bins);
  std::vector<float> frame(N);
  std::vector<float> harmMag(bins);
  std::vector<std::complex<float>> spec(bins);
  std::vector<float> timeBuf(N);
  const float p = cfg.maskPower;

  // Trung vị theo thời gian: mỗi bin một cửa sổ đã sắp xếp chứa |X| của các khung trong
  // [c − r, c + r] ∩ [0, J). Trung vị theo tần số: một cửa sổ trượt dọc các bin của khung c.
  std::vector<float> timeStore(bins * L);
  std::vector<SortedWindow> timeWin;
  timeWin.reserve(bins);
  for (std::size_t k = 0; k < bins; ++k) timeWin.emplace_back(&timeStore[k * L]);
  std::vector<float> freqStore(2 * rp + 1);
  SortedWindow freqWin(freqStore.data());

  auto addFrame = [&](std::size_t f) {
    const float* m = &magRing[(f % L) * bins];
    for (std::size_t k = 0; k < bins; ++k) timeWin[k].insert(m[k]);
  };
  auto removeFrame = [&](std::size_t f) {
    const float* m = &magRing[(f % L) * bins];
    for (std::size_t k = 0; k < bins; ++k) timeWin[k].erase(m[k]);
  };

  auto overlapAdd = [&](std::vector<float>& dst, std::size_t c) {
    const long long start = static_cast<long long>(c * H) - static_cast<long long>(pad);
    for (std::size_t i = 0; i < N; ++i) {
      const long long t = start + static_cast<long long>(i);
      if (t >= 0 && t < static_cast<long long>(n)) {
        dst[static_cast<std::size_t>(t)] += timeBuf[i] * w[i];
      }
    }
  };

  // Tách khung c; timeWin đang chứa đúng các khung lân cận của c.
  auto processCenter = [&](std::size_t c) {
    const float* mc = &magRing[(c % L) * bins];
    const std::complex<float>* xc = &specRing[(c % L) * bins];
    freqWin.clear();
    for (std::size_t q = 0; q <= std::min(rp, bins - 1); ++q) freqWin.insert(mc[q]);
    for (std::size_t k = 0; k < bins; ++k) {
      const float hMed = timeWin[k].median();  // H: trung vị theo thời gian
      const float pMed = freqWin.median();     // P: trung vị theo tần số, bin [k − rp, k + rp]
      // Trượt sang bin k + 1: xoá trước rồi mới chèn để không vượt sức chứa 2·rp + 1.
      if (k >= rp) freqWin.erase(mc[k - rp]);
      if (k + rp + 1 < bins) freqWin.insert(mc[k + rp + 1]);
      // Mặt nạ mềm; cả hai bằng 0 (im lặng) → chia đôi.
      const float hp = p == 2.0f ? hMed * hMed : std::pow(hMed, p);
      const float pp = p == 2.0f ? pMed * pMed : std::pow(pMed, p);
      const float sum = hp + pp;
      const float mh = sum > 1e-30f ? hp / sum : 0.5f;
      spec[k] = xc[k] * mh;
      harmMag[k] = mc[k] * mh;
    }
    if (onHarmonicFrame) {
      const double t =
          (static_cast<double>(c * H) - static_cast<double>(pad) + 0.5 * static_cast<double>(N)) /
          rate;
      onHarmonicFrame(harmMag.data(), bins, t);
    }
    fft.inverse(spec.data(), timeBuf.data());
    overlapAdd(out.harmonic, c);
    for (std::size_t k = 0; k < bins; ++k) spec[k] = xc[k] - spec[k];  // (1 − Mh)·X
    fft.inverse(spec.data(), timeBuf.data());
    overlapAdd(out.percussive, c);
  };

  for (std::size_t j = 0; j < J; ++j) {
    if (j >= L) removeFrame(j - L);  // trước khi ghi đè ô vòng của khung j − L
    const long long start = static_cast<long long>(j * H) - static_cast<long long>(pad);
    for (std::size_t i = 0; i < N; ++i) {
      const long long t = start + static_cast<long long>(i);
      frame[i] =
          t >= 0 && t < static_cast<long long>(n) ? x[static_cast<std::size_t>(t)] * w[i] : 0.0f;
    }
    std::complex<float>* dst = &specRing[(j % L) * bins];
    fft.forward(frame.data(), dst);
    float* mag = &magRing[(j % L) * bins];
    for (std::size_t k = 0; k < bins; ++k) {
      // NaN/inf (đầu vào hỏng) → 0: SortedWindow::erase không tìm thấy NaN và sẽ tràn cửa sổ.
      const float m = std::sqrt(std::norm(dst[k]));
      mag[k] = std::isfinite(m) ? m : 0.0f;
    }
    addFrame(j);
    if (j >= r) processCenter(j - r);  // cửa sổ = [j − 2r, j]
    if (j % 64 == 63 && onProgress && !onProgress(static_cast<float>(j) / static_cast<float>(J))) {
      out = HpssOutput{};
      return kResultCancelled;
    }
  }
  // Các khung cuối: cửa sổ co lại về phía cuối bài.
  for (std::size_t c = J > r ? J - r : 0; c < J; ++c) {
    if (c >= r + 1) removeFrame(c - r - 1);
    processCenter(c);
  }

  for (std::size_t i = 0; i < n; ++i) {
    const float inv = 1.0f / olaNorm[(i + pad) % H];
    out.harmonic[i] *= inv;
    out.percussive[i] *= inv;
  }
  if (onProgress && !onProgress(1.0f)) {
    out = HpssOutput{};
    return kResultCancelled;
  }
  return TC_OK;
}

}  // namespace tonecore
