#include "synth_voice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

namespace tonecore::test {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Formants {
  std::array<double, 3> freq;
  std::array<double, 3> bandwidth;
};

// Giá trị formant trung bình giọng người lớn (Peterson & Barney 1952, làm tròn).
Formants formantsFor(Vowel v) {
  switch (v) {
    case Vowel::I:
      return {{280.0, 2250.0, 2900.0}, {60.0, 100.0, 120.0}};
    case Vowel::U:
      return {{310.0, 870.0, 2250.0}, {60.0, 80.0, 120.0}};
    case Vowel::A:
    default:
      return {{750.0, 1150.0, 2600.0}, {70.0, 90.0, 120.0}};
  }
}

// Bộ cộng hưởng 2 cực, độ lợi DC = 1.
struct Resonator {
  double a1 = 0.0, a2 = 0.0, g = 1.0, y1 = 0.0, y2 = 0.0;
  Resonator(double f, double bw, double fs) {
    const double r = std::exp(-kPi * bw / fs);
    const double c = 2.0 * r * std::cos(2.0 * kPi * f / fs);
    a1 = c;
    a2 = -r * r;
    g = 1.0 - c + r * r;
  }
  double process(double x) {
    const double y = g * x + a1 * y1 + a2 * y2;
    y2 = y1;
    y1 = y;
    return y;
  }
};

// Xung thanh môn Rosenberg tại pha φ ∈ [0, 1): mở trong Tp, đóng trong Tn, còn lại khép.
double rosenberg(double phi) {
  constexpr double kTp = 0.40;
  constexpr double kTn = 0.16;
  if (phi < kTp) return 0.5 * (1.0 - std::cos(kPi * phi / kTp));
  if (phi < kTp + kTn) return std::cos(kPi * (phi - kTp) / (2.0 * kTn));
  return 0.0;
}

// Nội suy đường cong mượt qua các điểm điều khiển (p, giá trị) bằng Hermite (Catmull–Rom).
double smoothCurve(const std::vector<std::array<double, 2>>& pts, double p) {
  if (p <= pts.front()[0]) return pts.front()[1];
  if (p >= pts.back()[0]) return pts.back()[1];
  std::size_t i = 0;
  while (i + 1 < pts.size() && pts[i + 1][0] < p) ++i;
  const auto& p1 = pts[i];
  const auto& p2 = pts[i + 1];
  const double m1 = i > 0 ? (p2[1] - pts[i - 1][1]) / (p2[0] - pts[i - 1][0]) : 0.0;
  const double m2 = i + 2 < pts.size() ? (pts[i + 2][1] - p1[1]) / (pts[i + 2][0] - p1[0]) : 0.0;
  const double h = p2[0] - p1[0];
  const double t = (p - p1[0]) / h;
  const double t2 = t * t;
  const double t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p1[1] + (t3 - 2 * t2 + t) * h * m1 + (-2 * t3 + 3 * t2) * p2[1] +
         (t3 - t2) * h * m2;
}

}  // namespace

double midiToHz(double midi) {
  return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double hzToMidi(double hz) {
  return 69.0 + 12.0 * std::log2(hz / 440.0);
}

SynthResult synthVoice(const F0Contour& contour, double durationSec, const VoiceSpec& spec) {
  const double fs = spec.sampleRate;
  const auto n = static_cast<std::size_t>(std::llround(durationSec * fs));
  SynthResult r;
  r.audio.assign(n, 0.0f);
  r.f0.assign(n, 0.0f);

  std::mt19937 rng(spec.seed);
  std::normal_distribution<double> gauss(0.0, 1.0);
  const Formants fm = formantsFor(spec.vowel);
  std::array<Resonator, 3> res = {Resonator(fm.freq[0], fm.bandwidth[0], fs),
                                  Resonator(fm.freq[1], fm.bandwidth[1], fs),
                                  Resonator(fm.freq[2], fm.bandwidth[2], fs)};

  std::vector<double> y(n, 0.0);
  double phase = 0.0;
  double periodScale = 1.0;
  double ampScale = 1.0;
  double prevG = 0.0;
  double env = 0.0;  // bao biên độ: tránh bật/tắt đột ngột khi chuyển hữu thanh ↔ vô thanh
  bool voicedPrev = false;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / fs;
    const double f0 = contour(t);
    const bool voiced = f0 > 0.0;
    r.f0[i] = static_cast<float>(voiced ? f0 : 0.0);

    double g = 0.0;
    if (voiced) {
      if (!voicedPrev) phase = 0.0;
      phase += f0 * periodScale / fs;
      if (phase >= 1.0) {
        phase -= std::floor(phase);
        // Chu kỳ mới: lấy jitter / shimmer ngẫu nhiên.
        periodScale = 1.0 + spec.jitter * gauss(rng);
        ampScale = 1.0 + spec.shimmer * gauss(rng);
      }
      g = rosenberg(phase) * ampScale;
    }
    voicedPrev = voiced;
    // Bao biên độ lên/xuống ~5 ms.
    const double target = voiced ? 1.0 : 0.0;
    env += (target - env) * (1.0 - std::exp(-1.0 / (0.005 * fs)));

    // Đạo hàm (bức xạ ở môi ≈ vi phân bậc 1).
    double src = (g - prevG) * env;
    prevG = g;
    double v = src;
    for (auto& rz : res) v = rz.process(v);
    y[i] = v;
  }

  double peak = 0.0;
  for (double v : y) peak = std::max(peak, std::fabs(v));
  const double scale = peak > 0.0 ? spec.amplitude / peak : 0.0;
  const double noiseAmp = spec.amplitude * std::pow(10.0, spec.breathDb / 20.0);
  for (std::size_t i = 0; i < n; ++i) {
    r.audio[i] = static_cast<float>(y[i] * scale + noiseAmp * gauss(rng) * 0.5);
  }
  return r;
}

std::vector<float> whiteNoise(double durationSec, double sampleRate, double amplitude,
                              uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  std::vector<float> v(static_cast<std::size_t>(std::llround(durationSec * sampleRate)));
  for (auto& s : v) s = static_cast<float>(amplitude * dist(rng));
  return v;
}

std::vector<float> fricativeS(double durationSec, double sampleRate, double amplitude,
                              uint32_t seed) {
  auto v = whiteNoise(durationSec, sampleRate, 1.0, seed);
  // Thông cao 4 kHz bậc 4 (2 biquad RBJ, Q = 0,7071) — phổ tiếng "s" tập trung 4–10 kHz.
  const double w0 = 2.0 * kPi * 4000.0 / sampleRate;
  const double alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
  const double a0 = 1.0 + alpha;
  const double b0 = (1.0 + std::cos(w0)) / 2.0 / a0;
  const double b1 = -(1.0 + std::cos(w0)) / a0;
  const double a1 = -2.0 * std::cos(w0) / a0;
  const double a2 = (1.0 - alpha) / a0;
  for (int stage = 0; stage < 2; ++stage) {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (auto& s : v) {
      const double x = s;
      const double yv = b0 * x + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = yv;
      s = static_cast<float>(yv);
    }
  }
  float peak = 0.0f;
  for (float s : v) peak = std::max(peak, std::fabs(s));
  for (auto& s : v) s = static_cast<float>(s / peak * amplitude);
  return v;
}

F0Contour steady(double hz) {
  return [hz](double) { return hz; };
}

F0Contour vibrato(double hz, double rateHz, double depthCents) {
  return [=](double t) {
    return hz * std::pow(2.0, depthCents / 1200.0 * std::sin(2.0 * kPi * rateHz * t));
  };
}

F0Contour glide(double fromHz, double toHz, double startSec, double durSec) {
  return [=](double t) {
    const double p = std::clamp((t - startSec) / durSec, 0.0, 1.0);
    return fromHz * std::pow(toHz / fromHz, p);
  };
}

F0Contour offset(double hz, double cents) {
  const double f = hz * std::pow(2.0, cents / 1200.0);
  return [f](double) { return f; };
}

double vietToneShape(VietTone tone, double p) {
  p = std::clamp(p, 0.0, 1.0);
  switch (tone) {
    case VietTone::Ngang:
      return 0.0;
    case VietTone::Huyen:
      return smoothCurve({{0.0, -0.5}, {0.5, -1.8}, {1.0, -3.0}}, p);
    case VietTone::Sac:
      return smoothCurve({{0.0, 0.0}, {0.3, -0.5}, {0.7, 1.8}, {1.0, 4.0}}, p);
    case VietTone::Hoi:
      return smoothCurve({{0.0, 0.0}, {0.3, -1.5}, {0.6, -3.0}, {1.0, -1.0}}, p);
    case VietTone::Nga:
      // Có ngắt thanh hầu ngắn ở giữa (vô thanh 0,42–0,5) rồi vọt lên cao.
      if (p > 0.42 && p < 0.5) return std::numeric_limits<double>::quiet_NaN();
      return smoothCurve({{0.0, 0.0}, {0.4, -1.0}, {0.5, 0.5}, {1.0, 4.0}}, p);
    case VietTone::Nang:
      // Rơi nhanh rồi tắc thanh hầu (vô thanh từ 0,75).
      if (p > 0.75) return std::numeric_limits<double>::quiet_NaN();
      return smoothCurve({{0.0, -0.5}, {0.4, -1.5}, {0.75, -4.0}}, p);
  }
  return 0.0;
}

F0Contour vietTone(VietTone tone, double baseHz, double syllableSec) {
  return [=](double t) {
    const double p = std::fmod(t, syllableSec) / syllableSec;
    const double st = vietToneShape(tone, p);
    if (std::isnan(st)) return 0.0;
    return baseHz * std::pow(2.0, st / 12.0);
  };
}

}  // namespace tonecore::test
