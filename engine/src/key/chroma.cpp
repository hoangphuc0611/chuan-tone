#include "key/chroma.h"

#include <algorithm>
#include <cmath>

namespace tonecore {

Chromagram computeChroma(const CqtSpectrogram& cqt, const ChromaConfig& cfg) {
  Chromagram out;
  out.frameSec = cqt.frameSec;
  const int g = cqt.binsPerOctave / 12;  // số bin mỗi nửa cung
  if (g < 1 || cqt.binsPerOctave % 12 != 0) return out;
  const int notes = cqt.bins / g;
  const std::size_t F = cqt.frames;
  out.bass.assign(F, ChromaVector{});
  out.treble.assign(F, ChromaVector{});
  out.energyDb.assign(F, -200.0f);
  if (F == 0) return out;

  // Nốt n gồm các bin [n·g, n·g + g); MIDI của nốt = cao độ bin giữa, chưa tính tuning.
  std::vector<int> pitchClass(static_cast<std::size_t>(notes));
  std::vector<float> bassWeight(static_cast<std::size_t>(notes));
  const double span = std::max(1e-9, cfg.trebleFullMidi - cfg.bassFullMidi);
  for (int n = 0; n < notes; ++n) {
    const double centerMidi = cqt.minMidi + 12.0 * (n * g + 0.5 * (g - 1)) / cqt.binsPerOctave;
    const auto midi = static_cast<int>(std::lround(centerMidi));
    pitchClass[static_cast<std::size_t>(n)] = ((midi % 12) + 12) % 12;
    bassWeight[static_cast<std::size_t>(n)] =
        static_cast<float>(std::clamp((cfg.trebleFullMidi - midi) / span, 0.0, 1.0));
  }

  std::vector<float> total(F, 0.0f);
  for (std::size_t i = 0; i < F; ++i) {
    const float* p = cqt.frame(i);
    for (int n = 0; n < notes; ++n) {
      float e = 0.0f;
      for (int j = 0; j < g; ++j) e += p[n * g + j];
      const auto ni = static_cast<std::size_t>(n);
      const auto pc = static_cast<std::size_t>(pitchClass[ni]);
      out.bass[i][pc] += bassWeight[ni] * e;
      out.treble[i][pc] += (1.0f - bassWeight[ni]) * e;
      total[i] += e;
    }
    out.energyDb[i] = static_cast<float>(10.0 * std::log10(static_cast<double>(total[i]) + 1e-20));
  }

  // Mức tham chiếu: phân vị 95 công suất khung (bền với vài khung đột biến).
  std::vector<float> sorted = total;
  const auto k = static_cast<std::ptrdiff_t>((sorted.size() - 1) * 95 / 100);
  std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
  const float ref =
      sorted[static_cast<std::size_t>(k)] > 0.0f ? sorted[static_cast<std::size_t>(k)] : 1.0f;
  const float scale = cfg.compression / ref;
  for (std::size_t i = 0; i < F; ++i) {
    for (std::size_t pc = 0; pc < 12; ++pc) {
      out.bass[i][pc] = std::log1p(scale * out.bass[i][pc]);
      out.treble[i][pc] = std::log1p(scale * out.treble[i][pc]);
    }
  }
  return out;
}

}  // namespace tonecore
