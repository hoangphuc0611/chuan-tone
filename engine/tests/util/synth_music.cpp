#include "util/synth_music.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace tonecore::test {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double midiToHz(double midi, double tuningCents) {
  return 440.0 * std::pow(2.0, (midi - 69.0 + tuningCents / 100.0) / 12.0);
}

void addNote(std::vector<float>& buf, double rate, double startSec, double durSec, double midi,
             float amplitude, const NoteSpec& spec) {
  const double f0 = midiToHz(midi, spec.tuningCents);
  const auto begin = static_cast<long long>(std::llround(startSec * rate));
  const auto len = static_cast<long long>(std::llround(durSec * rate));
  const double attack = 0.010 * rate;
  const double release = 0.050 * rate;
  for (long long i = 0; i < len; ++i) {
    const long long t = begin + i;
    if (t < 0 || t >= static_cast<long long>(buf.size())) continue;
    const double di = static_cast<double>(i);
    double env = std::min(1.0, di / attack) * std::min(1.0, static_cast<double>(len - i) / release);
    if (spec.decayPerSec > 0.0) env *= std::exp(-spec.decayPerSec * di / rate);
    double s = 0.0;
    for (int h = 1; h <= spec.harmonics; ++h) {
      const double f = f0 * h;
      if (f >= 0.45 * rate) break;
      s += std::sin(2.0 * kPi * f * di / rate) / h;
    }
    buf[static_cast<std::size_t>(t)] += static_cast<float>(amplitude * env * s);
  }
}

void addClick(std::vector<float>& buf, double rate, double timeSec, float amplitude,
              uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
  const auto begin = static_cast<long long>(std::llround(timeSec * rate));
  const auto len = static_cast<long long>(0.030 * rate);
  for (long long i = 0; i < len; ++i) {
    const long long t = begin + i;
    if (t < 0 || t >= static_cast<long long>(buf.size())) continue;
    const double env = std::exp(-static_cast<double>(i) / (0.004 * rate));
    buf[static_cast<std::size_t>(t)] += static_cast<float>(amplitude * env) * uni(rng);
  }
}

void addDrum(std::vector<float>& buf, double rate, double timeSec, Drum drum, float amplitude,
             uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> uni(-1.0, 1.0);
  const auto begin = static_cast<long long>(std::llround(timeSec * rate));
  const auto len = static_cast<long long>(0.4 * rate);
  double prevNoise = 0.0;
  for (long long i = 0; i < len; ++i) {
    const long long t = begin + i;
    if (t < 0 || t >= static_cast<long long>(buf.size())) continue;
    const double s = static_cast<double>(i) / rate;
    const double noise = uni(rng);
    double v = 0.0;
    switch (drum) {
      case Drum::Kick: {
        // Pha = 2π ∫ f(t) dt với f(t) = 50 + 100·e^(−t/0,03).
        const double phase = 2.0 * kPi * (50.0 * s + 100.0 * 0.03 * (1.0 - std::exp(-s / 0.03)));
        v = std::sin(phase) * std::exp(-s / 0.05) + 0.5 * noise * std::exp(-s / 0.0005);
        break;
      }
      case Drum::Snare:
        v = 0.7 * noise * std::exp(-s / 0.04) +
            0.5 * std::sin(2.0 * kPi * 185.0 * s) * std::exp(-s / 0.03);
        break;
      case Drum::HiHat:
        v = (noise - prevNoise) * 0.5 * std::exp(-s / 0.012);
        break;
    }
    prevNoise = noise;
    buf[static_cast<std::size_t>(t)] += static_cast<float>(amplitude * v);
  }
}

void addCadence(std::vector<float>& buf, double rate, double startSec, int root, bool minor,
                int loops, double chordSec, uint32_t seed, const NoteSpec& spec) {
  // Bậc (nửa cung tính từ chủ âm) của các nốt trong 4 hợp âm.
  static const int kMajor[4][3] = {{0, 4, 7}, {5, 9, 0}, {7, 11, 2}, {0, 4, 7}};
  static const int kMinor[4][3] = {{0, 3, 7}, {5, 8, 0}, {7, 11, 2}, {0, 3, 7}};
  static const int kChordRoot[4] = {0, 5, 7, 0};
  std::mt19937 rng(seed);
  for (int l = 0; l < loops; ++l) {
    for (int c = 0; c < 4; ++c) {
      const double t = startSec + (4 * l + c) * chordSec;
      const int (&degrees)[3] = minor ? kMinor[c] : kMajor[c];
      const int inversion = static_cast<int>(rng() % 3);
      int midi = 55 + static_cast<int>(rng() % 8);  // nốt thấp nhất của hợp âm ≥ G3…D4
      for (int i = 0; i < 3; ++i) {
        const int pc = (root + degrees[(inversion + i) % 3]) % 12;
        while ((midi % 12) != pc) ++midi;
        addNote(buf, rate, t, chordSec, midi, 0.06f, spec);
      }
      int bass = 40;  // E2
      while ((bass % 12) != (root + kChordRoot[c]) % 12) ++bass;
      addNote(buf, rate, t, chordSec, bass, 0.08f, spec);
    }
  }
}

}  // namespace tonecore::test
