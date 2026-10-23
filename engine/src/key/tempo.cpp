#include "key/tempo.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "dsp/fft.h"

namespace tonecore {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool validConfig(const TempoConfig& c) {
  const bool pow2 = c.fftSize >= 16 && (c.fftSize & (c.fftSize - 1)) == 0;
  return pow2 && c.hop > 0 && c.minBpm > 0.0f && c.maxBpm > c.minBpm && c.priorBpm > 0.0f &&
         c.priorOctaves > 0.0f && c.combHarmonics >= 1;
}

}  // namespace

std::vector<float> onsetEnvelope(const std::vector<float>& x, double rate, const TempoConfig& cfg) {
  const std::size_t N = cfg.fftSize;
  const std::size_t H = cfg.hop;
  const std::size_t bins = N / 2 + 1;
  if (x.size() < N) return {};
  const std::size_t J = (x.size() - N) / H + 1;

  std::vector<float> w(N);
  double sw = 0.0;
  for (std::size_t i = 0; i < N; ++i) {
    w[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / N));
    sw += static_cast<double>(w[i]);
  }
  // |X| của sin biên độ A ≈ A·Σw/2 → chia cho Σw/2 để γ không phụ thuộc kích thước FFT.
  const auto norm = static_cast<float>(2.0 / sw);

  RealFft fft(N);
  std::vector<float> frame(N);
  std::vector<std::complex<float>> spec(bins);
  std::vector<float> prev(bins, 0.0f);
  std::vector<float> cur(bins);
  std::vector<float> flux(J, 0.0f);
  for (std::size_t j = 0; j < J; ++j) {
    const float* src = x.data() + j * H;
    for (std::size_t i = 0; i < N; ++i) frame[i] = src[i] * w[i];
    fft.forward(frame.data(), spec.data());
    float f = 0.0f;
    for (std::size_t k = 0; k < bins; ++k) {
      cur[k] = std::log1p(cfg.compression * norm * std::sqrt(std::norm(spec[k])));
      if (j > 0) f += std::max(0.0f, cur[k] - prev[k]);
    }
    flux[j] = f;
    prev.swap(cur);
  }

  // Trừ trung bình trượt (tổng tiền tố) rồi cắt phần âm: chỉ giữ chỗ nổi hẳn lên so với xung quanh.
  const double frameRate = rate / static_cast<double>(H);
  const auto half = static_cast<std::size_t>(
      std::max(1.0, std::round(0.5 * static_cast<double>(cfg.meanWindowSec) * frameRate)));
  std::vector<double> prefix(J + 1, 0.0);
  for (std::size_t j = 0; j < J; ++j) prefix[j + 1] = prefix[j] + static_cast<double>(flux[j]);
  std::vector<float> env(J);
  for (std::size_t j = 0; j < J; ++j) {
    const std::size_t a = j >= half ? j - half : 0;
    const std::size_t b = std::min(J, j + half + 1);
    const double mean = (prefix[b] - prefix[a]) / static_cast<double>(b - a);
    env[j] = static_cast<float>(std::max(0.0, static_cast<double>(flux[j]) - mean));
  }
  return env;
}

tc_result estimateTempo(const std::vector<float>& x, double rate, const TempoConfig& cfg,
                        const ProgressFn& onProgress, std::optional<TempoEstimate>& out) {
  out.reset();
  if (!validConfig(cfg) || !(rate > 0.0)) return TC_ERR_INVALID_ARG;
  if (static_cast<double>(x.size()) / rate < static_cast<double>(cfg.minDurationSec)) {
    return TC_OK;
  }
  std::vector<float> env = onsetEnvelope(x, rate, cfg);
  if (onProgress && !onProgress(0.5f)) return kResultCancelled;
  const std::size_t M = env.size();
  const double frameRate = rate / static_cast<double>(cfg.hop);

  // Tự tương quan của đường onset đã trừ trung bình, cho các độ trễ cần tới (cả bội của răng lược).
  double mean = 0.0;
  for (const float v : env) mean += static_cast<double>(v);
  mean /= static_cast<double>(std::max<std::size_t>(1, M));
  std::vector<double> e(M);
  for (std::size_t i = 0; i < M; ++i) e[i] = static_cast<double>(env[i]) - mean;

  const auto lagMin = static_cast<std::size_t>(
      std::max(1.0, std::floor(60.0 * frameRate / static_cast<double>(cfg.maxBpm))));
  const auto lagMax =
      static_cast<std::size_t>(std::ceil(60.0 * frameRate / static_cast<double>(cfg.minBpm)));
  const std::size_t harmonics = static_cast<std::size_t>(cfg.combHarmonics);
  const std::size_t rMax = std::min(M > 0 ? M - 1 : 0, (lagMax + 1) * harmonics);
  if (lagMax + 1 >= M) return TC_OK;  // bài quá ngắn so với chu kỳ chậm nhất
  std::vector<double> r(rMax + 1, 0.0);
  for (std::size_t tau = 0; tau <= rMax; ++tau) {
    double acc = 0.0;
    for (std::size_t i = tau; i < M; ++i) acc += e[i] * e[i - tau];
    r[tau] = acc;
  }
  if (!(r[0] > 1e-12)) return TC_OK;  // không có tiếng gõ
  if (onProgress && !onProgress(0.9f)) return kResultCancelled;

  // Răng lược × prior; chọn đỉnh rồi nội suy parabol.
  const double tau0 = 60.0 * frameRate / static_cast<double>(cfg.priorBpm);
  const double sigma = static_cast<double>(cfg.priorOctaves);
  auto score = [&](std::size_t tau) {
    double s = 0.0;
    for (std::size_t m = 1; m <= harmonics && m * tau <= rMax; ++m) {
      s += r[m * tau] / static_cast<double>(m);
    }
    const double oct = std::log2(static_cast<double>(tau) / tau0) / sigma;
    return s * std::exp(-0.5 * oct * oct);
  };
  std::vector<double> sc(lagMax + 2, 0.0);
  std::size_t best = lagMin;
  for (std::size_t tau = lagMin; tau <= lagMax + 1; ++tau) {
    sc[tau] = score(tau);
    if (tau <= lagMax && sc[tau] > sc[best]) best = tau;
  }
  if (!(sc[best] > 0.0)) return TC_OK;
  double tauFrac = static_cast<double>(best);
  if (best > lagMin) {
    const double ym = sc[best - 1];
    const double y0 = sc[best];
    const double yp = sc[best + 1];
    const double denom = ym - 2.0 * y0 + yp;
    if (denom < 0.0) tauFrac += std::clamp(0.5 * (ym - yp) / denom, -0.5, 0.5);
  }
  TempoEstimate est;
  est.bpm = static_cast<float>(60.0 * frameRate / tauFrac);
  est.confidence = static_cast<float>(std::clamp(r[best] / r[0], 0.0, 1.0));
  out = est;
  if (onProgress && !onProgress(1.0f)) {
    out.reset();
    return kResultCancelled;
  }
  return TC_OK;
}

}  // namespace tonecore
