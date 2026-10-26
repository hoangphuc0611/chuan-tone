// Tốc độ đổi tone beat (M4): render 10 s beat tổng hợp stereo 48 kHz, báo "× thời gian thực"
// (realtime_x = giây audio / giây CPU). Chỉ tiêu: ≥ 100× trên Mac M1 (bài 4 phút ≤ 2,4 s) → ngân
// sách ≤ 4 s trên iPhone 13 Pro (docs/stretch-results.md).

#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

#include "stretch/pitch_shifter.h"

namespace {

std::vector<float> fakeBeat(std::size_t frames) {
  std::vector<float> x(2 * frames);
  uint32_t state = 1;
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    const double beat = std::fmod(t, 0.5);
    state = state * 1664525u + 1013904223u;
    const double noise = static_cast<double>(state >> 8) / 16777216.0 - 0.5;
    double s = 0.3 * std::exp(-beat / 0.05) * std::sin(2 * 3.14159265 * 60.0 * beat) +
               0.1 * noise * std::exp(-std::fmod(t, 0.25) / 0.02);
    for (const double f : {220.0, 277.18, 329.63}) s += 0.08 * std::sin(2 * 3.14159265 * f * t);
    x[2 * i] = static_cast<float>(s);
    x[2 * i + 1] = static_cast<float>(0.9 * s);
  }
  return x;
}

void BM_PitchShifter(benchmark::State& state) {
  const auto semitones = static_cast<double>(state.range(0));
  constexpr std::size_t kFrames = 48000 * 10;
  const auto beat = fakeBeat(kFrames);
  std::vector<float> out(beat.size());
  for (auto _ : state) {
    tonecore::PitchShifter ps(2, semitones);
    ps.start(beat.data(), kFrames, 0);
    for (std::size_t pos = 0; pos < kFrames; pos += 4096) {
      const auto n = static_cast<uint32_t>(std::min<std::size_t>(4096, kFrames - pos));
      ps.pull(out.data() + 2 * pos, n);
    }
    benchmark::DoNotOptimize(out.data());
  }
  state.counters["realtime_x"] = benchmark::Counter(10.0 * static_cast<double>(state.iterations()),
                                                    benchmark::Counter::kIsRate);
}
BENCHMARK(BM_PitchShifter)->Arg(-6)->Arg(4)->Arg(6)->Unit(benchmark::kMillisecond);

}  // namespace
