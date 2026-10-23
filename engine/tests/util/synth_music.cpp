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

}  // namespace tonecore::test
