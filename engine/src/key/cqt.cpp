#include "key/cqt.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

#include "dsp/decimator.h"
#include "dsp/fft.h"

namespace tonecore {

namespace {

constexpr double kPi = 3.14159265358979323846;

double midiToHz(double midi) {
  return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

struct KernelTap {
  uint32_t fftBin;
  std::complex<float> value;  // K*[j] / L
};

// Kernel phổ thưa cho một quãng 8, tần số chuẩn hoá nu[b] = f_b / rate.
std::vector<std::vector<KernelTap>> buildKernel(const std::vector<double>& nu, double q,
                                                std::size_t L, float threshold) {
  std::vector<std::vector<KernelTap>> kernel(nu.size());
  const std::size_t maxBin = L / 2;
  for (std::size_t b = 0; b < nu.size(); ++b) {
    const auto len = static_cast<std::size_t>(std::ceil(q / nu[b]));
    const std::size_t off = (L - len) / 2;  // đặt cửa sổ giữa khung
    std::vector<double> w(len);
    double sw = 0.0;
    for (std::size_t m = 0; m < len; ++m) {
      w[m] = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(m) + 0.5) / len);
      sw += w[m];
    }
    // DFT của k[n] = w[n − off]·e^{i2πν(n − off − c)} / Σw chỉ đáng kể quanh j ≈ ν·L: búp chính
    // Hann rộng ±2·L/len bin, lấy rộng thêm để giữ vài búp phụ đầu.
    const double center = nu[b] * static_cast<double>(L);
    const double span = 4.0 * static_cast<double>(L) / static_cast<double>(len) + 2.0;
    const auto j0 = static_cast<std::size_t>(std::max(0.0, std::floor(center - span)));
    const auto j1 = std::min(maxBin, static_cast<std::size_t>(std::ceil(center + span)));
    const double c = 0.5 * (static_cast<double>(len) - 1.0);
    std::vector<std::complex<double>> k(j1 - j0 + 1);
    double peak = 0.0;
    for (std::size_t j = j0; j <= j1; ++j) {
      std::complex<double> acc = 0.0;
      for (std::size_t m = 0; m < len; ++m) {
        const double phase = 2.0 * kPi *
                             (nu[b] * (static_cast<double>(m) - c) -
                              static_cast<double>(j) * static_cast<double>(m + off) / L);
        acc += w[m] * std::complex<double>(std::cos(phase), std::sin(phase));
      }
      k[j - j0] = acc / sw;
      peak = std::max(peak, std::abs(k[j - j0]));
    }
    for (std::size_t j = j0; j <= j1; ++j) {
      const std::complex<double> v = k[j - j0];
      if (std::abs(v) < static_cast<double>(threshold) * peak) continue;
      kernel[b].push_back(
          {static_cast<uint32_t>(j), std::complex<float>(std::conj(v) / static_cast<double>(L))});
    }
  }
  return kernel;
}

std::size_t nextPow2(std::size_t v) {
  std::size_t p = 16;
  while (p < v) p <<= 1;
  return p;
}

}  // namespace

tc_result computeCqt(const std::vector<float>& x, double rate, const CqtConfig& cfg,
                     const ProgressFn& onProgress, CqtSpectrogram& out) {
  out = CqtSpectrogram{};
  const int B = cfg.binsPerOctave;
  const int O = cfg.octaves;
  if (!(rate > 0.0) || B < 12 || O < 1 || O > 10 || cfg.hopTop == 0 || cfg.decimatorTaps < 4) {
    return TC_ERR_INVALID_ARG;
  }

  // Tần số các bin của quãng 8 cao nhất (đã dời theo tuning).
  const double topLowMidi =
      cfg.minMidi + 12.0 * (O - 1) + static_cast<double>(cfg.tuningCents) / 100.0;
  std::vector<double> nu(static_cast<std::size_t>(B));
  for (int b = 0; b < B; ++b)
    nu[static_cast<std::size_t>(b)] = midiToHz(topLowMidi + 12.0 * b / B) / rate;
  if (nu.back() >= 0.45) return TC_ERR_INVALID_ARG;
  const double q = 1.0 / (std::pow(2.0, 1.0 / B) - 1.0);
  const std::size_t L = nextPow2(static_cast<std::size_t>(std::ceil(q / nu.front())));
  const auto kernel = buildKernel(nu, q, L, cfg.kernelThreshold);

  const std::size_t n = x.size();
  const std::size_t hop = cfg.hopTop;
  const std::size_t outHop = hop << (O - 1);  // bước khung đầu ra, mẫu gốc
  const std::size_t F = n == 0 ? 0 : (n - 1) / outHop + 1;
  out.binsPerOctave = B;
  out.bins = B * O;
  out.minMidi = cfg.minMidi;
  out.tuningCents = cfg.tuningCents;
  out.frameSec = static_cast<double>(outHop) / rate;
  out.frames = F;
  out.power.assign(F * static_cast<std::size_t>(out.bins), 0.0f);
  if (F == 0) return TC_OK;
  std::vector<uint32_t> count(F * static_cast<std::size_t>(O), 0);

  // Tổng số khung (để báo tiến độ): quãng o có ~n / (hop · 2^o) khung.
  double totalFrames = 0.0;
  for (int o = 0; o < O; ++o)
    totalFrames += static_cast<double>(n) / static_cast<double>(hop << o) + 1.0;
  double doneFrames = 0.0;

  RealFft fft(L);
  std::vector<float> frame(L);
  std::vector<std::complex<float>> spec(L / 2 + 1);
  std::vector<float> level;  // tín hiệu của quãng 8 hiện tại (o ≥ 1)
  std::vector<float> next;
  const float* sig = x.data();
  std::size_t len = n;
  // Mẫu s của quãng o ↔ thời điểm a·s + bOff ở tần số gốc (bù trễ (taps−1)/2 của mỗi lần hạ).
  double a = 1.0;
  double bOff = 0.0;
  const double d = 0.5 * (cfg.decimatorTaps - 1);

  for (int o = 0; o < O; ++o) {
    if (o > 0) {
      Decimator dec(2, cfg.decimatorTaps, 0.5, 6.0);
      next.resize(len / 2 + 1);
      next.resize(dec.process(sig, len, next.data()));
      level.swap(next);
      sig = level.data();
      len = level.size();
      bOff += a * (1.0 - d);
      a *= 2.0;
    }
    const auto binBase = static_cast<std::size_t>((O - 1 - o) * B);
    for (std::size_t c = 0; c < len; c += hop) {
      // Khung dài L có tâm tại mẫu c (đệm 0 ngoài biên); cửa sổ đã nằm trong kernel.
      const long long start = static_cast<long long>(c) - static_cast<long long>(L / 2);
      for (std::size_t i = 0; i < L; ++i) {
        const long long t = start + static_cast<long long>(i);
        frame[i] = t >= 0 && t < static_cast<long long>(len) ? sig[t] : 0.0f;
      }
      const double tOrig = a * static_cast<double>(c) + bOff;
      const long long fi = std::llround(tOrig / static_cast<double>(outHop));
      if (fi >= 0 && fi < static_cast<long long>(F)) {
        fft.forward(frame.data(), spec.data());
        float* dst = &out.power[static_cast<std::size_t>(fi) * static_cast<std::size_t>(out.bins)];
        for (std::size_t b = 0; b < kernel.size(); ++b) {
          std::complex<float> acc = 0.0f;
          for (const KernelTap& tap : kernel[b]) acc += spec[tap.fftBin] * tap.value;
          dst[binBase + b] += std::norm(acc);
        }
        ++count[static_cast<std::size_t>(fi) * static_cast<std::size_t>(O) +
                static_cast<std::size_t>(o)];
      }
      doneFrames += 1.0;
      if (static_cast<std::size_t>(doneFrames) % 256 == 0 && onProgress &&
          !onProgress(static_cast<float>(std::min(1.0, doneFrames / totalFrames)))) {
        out = CqtSpectrogram{};
        return kResultCancelled;
      }
    }
  }

  // Trung bình công suất các khung của từng quãng 8 rơi vào cùng khung đầu ra.
  for (std::size_t i = 0; i < F; ++i) {
    for (int o = 0; o < O; ++o) {
      const uint32_t cnt = count[i * static_cast<std::size_t>(O) + static_cast<std::size_t>(o)];
      if (cnt <= 1) continue;
      float* dst = &out.power[i * static_cast<std::size_t>(out.bins) +
                              static_cast<std::size_t>((O - 1 - o) * B)];
      const float inv = 1.0f / static_cast<float>(cnt);
      for (int b = 0; b < B; ++b) dst[b] *= inv;
    }
  }
  if (onProgress && !onProgress(1.0f)) {
    out = CqtSpectrogram{};
    return kResultCancelled;
  }
  return TC_OK;
}

}  // namespace tonecore
