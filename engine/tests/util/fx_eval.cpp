#include "util/fx_eval.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "fx/svf.h"

namespace tonecore::test {

namespace {

constexpr double kPi = 3.14159265358979323846;

void runBlocks(const MonoProcess& process, std::vector<float>& x, uint32_t block) {
  for (std::size_t i = 0; i < x.size(); i += block) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(block, x.size() - i));
    process(x.data() + i, n);
  }
}

// Lọc tại chỗ bằng SVF (hai lần → bậc 4).
void filter(std::vector<float>& x, SvfType type, double f, double fs) {
  const SvfCoeffs c = SvfCoeffs::make(type, fs, f, 0.7071067811865476, 0.0);
  for (int pass = 0; pass < 2; ++pass) {
    Svf s;
    for (float& v : x) v = s.process(v, c);
  }
}

}  // namespace

std::vector<float> impulseResponse(const MonoProcess& process, std::size_t n, uint32_t block) {
  std::vector<float> x(n, 0.0f);
  if (n > 0) x[0] = 1.0f;
  runBlocks(process, x, block);
  return x;
}

double magnitudeDb(const std::vector<float>& ir, double f, double sampleRate) {
  const double w = 2.0 * kPi * f / sampleRate;
  // Quay pha bằng phép nhân dồn (ổn định đủ cho ≤ vài giây ở double).
  const std::complex<double> step = std::polar(1.0, -w);
  std::complex<double> rot(1.0, 0.0);
  std::complex<double> sum(0.0, 0.0);
  for (const float v : ir) {
    sum += static_cast<double>(v) * rot;
    rot *= step;
  }
  return 20.0 * std::log10(std::abs(sum) + 1e-300);
}

std::vector<double> logFrequencies(int count, double from, double to) {
  std::vector<double> f(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    f[static_cast<std::size_t>(i)] =
        from * std::pow(to / from, static_cast<double>(i) / static_cast<double>(count - 1));
  }
  return f;
}

double rt60(const std::vector<float>& ir, double sampleRate, double bandLow, double bandHigh) {
  std::vector<float> x = ir;
  if (bandLow > 0.0) filter(x, SvfType::HighPass, bandLow, sampleRate);
  if (bandHigh > 0.0) filter(x, SvfType::LowPass, bandHigh, sampleRate);
  // EDC(t) = Σ_{τ ≥ t} h²(τ), chuẩn hoá theo EDC(0), theo dB.
  std::vector<double> edc(x.size());
  double acc = 0.0;
  for (std::size_t i = x.size(); i-- > 0;) {
    acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    edc[i] = acc;
  }
  if (acc <= 0.0) return 0.0;
  // Khớp thẳng (bình phương nhỏ nhất) trên đoạn EDC trong [−35, −5] dB.
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < edc.size(); ++i) {
    const double db = 10.0 * std::log10(edc[i] / acc + 1e-300);
    if (db > -5.0) continue;
    if (db < -35.0) break;
    const double t = static_cast<double>(i) / sampleRate;
    sx += t;
    sy += db;
    sxx += t * t;
    sxy += t * db;
    ++count;
  }
  if (count < 2) return 0.0;
  const auto n = static_cast<double>(count);
  const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);  // dB / giây (âm)
  return slope < 0.0 ? -60.0 / slope : 0.0;
}

std::vector<double> staticCurveDb(const MonoProcess& process, const std::vector<double>& levelsDb,
                                  double freq, double sampleRate, double settleSec) {
  std::vector<double> out;
  const auto n = static_cast<std::size_t>(settleSec * sampleRate);
  const auto tail = static_cast<std::size_t>(0.1 * sampleRate);
  for (const double db : levelsDb) {
    const double amp = std::pow(10.0, db / 20.0);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
      x[i] = static_cast<float>(amp *
                                std::sin(2.0 * kPi * freq * static_cast<double>(i) / sampleRate));
    }
    runBlocks(process, x, 256);
    double e = 0.0;
    for (std::size_t i = n - tail; i < n; ++i) e += static_cast<double>(x[i]) * x[i];
    const double rms = std::sqrt(e / static_cast<double>(tail));
    out.push_back(20.0 * std::log10(rms + 1e-300) + 3.0103);
  }
  return out;
}

double energyAboveDb(const std::vector<float>& x, double cutoffHz, double sampleRate) {
  std::vector<float> hi = x;
  filter(hi, SvfType::HighPass, cutoffHz, sampleRate);
  double eh = 0.0, et = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    eh += static_cast<double>(hi[i]) * hi[i];
    et += static_cast<double>(x[i]) * x[i];
  }
  return 10.0 * std::log10((eh + 1e-300) / (et + 1e-300));
}

}  // namespace tonecore::test
