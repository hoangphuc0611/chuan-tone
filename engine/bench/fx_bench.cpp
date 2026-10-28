// CPU của các khối FX giọng (M5), mono 48 kHz, khối 128 mẫu (như iPhone). Ngân sách: cả chuỗi
// giọng (chỉnh giọng + FX) ≤ 25% một nhân trên máy tầm trung (CLAUDE.md 7).

#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

#include "fx/compressor.h"
#include "fx/de_esser.h"
#include "fx/equalizer.h"
#include "fx/noise_gate.h"

namespace {

constexpr double kFs = 48000.0;
constexpr uint32_t kBlock = 128;

std::vector<float> voiceLike(std::size_t n) {
  std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kFs;
    x[i] = static_cast<float>(0.2 * std::sin(2 * 3.14159265 * 220.0 * t) +
                              0.05 * std::sin(2 * 3.14159265 * 3300.0 * t));
  }
  return x;
}

tonecore::EqParams fullEq() {
  tonecore::EqParams p;
  p.highPassHz = 80.0f;
  p.lowPassHz = 9000.0f;
  p.bands[0] = {tonecore::SvfType::Bell, 300.0f, -2.0f, 1.0f};
  p.bands[1] = {tonecore::SvfType::Bell, 3000.0f, 2.0f, 1.0f};
  p.bands[2] = {tonecore::SvfType::Bell, 5000.0f, 3.0f, 1.0f};
  p.bands[3] = {tonecore::SvfType::LowShelf, 150.0f, -2.0f, 0.7f};
  p.bands[4] = {tonecore::SvfType::HighShelf, 12000.0f, 2.0f, 0.7f};
  return p;
}

// "realtime_x" = giây audio xử lý được trong một giây CPU; % một nhân = 100 / realtime_x.
void report(benchmark::State& state, std::size_t samples) {
  state.counters["realtime_x"] = benchmark::Counter(static_cast<double>(samples) / kFs,
                                                    benchmark::Counter::kIsIterationInvariantRate);
}

// EQ đủ 7 tầng (thông cao + 5 băng + thông thấp), thông số đứng yên.
void BM_Equalizer(benchmark::State& state) {
  tonecore::Equalizer eq(kFs);
  eq.setParamsImmediate(fullEq());
  auto x = voiceLike(kBlock);
  for (auto _ : state) {
    eq.process(x.data(), kBlock);
    benchmark::DoNotOptimize(x.data());
  }
  report(state, kBlock);
}
BENCHMARK(BM_Equalizer);

// Như trên nhưng thông số đang trượt (người dùng kéo thanh trượt): tính lại hệ số mỗi 16 mẫu.
void BM_EqualizerMoving(benchmark::State& state) {
  tonecore::Equalizer eq(kFs);
  auto x = voiceLike(kBlock);
  int round = 0;
  for (auto _ : state) {
    auto p = fullEq();
    p.bands[1].freqHz = (round++ % 2 == 0) ? 3000.0f : 3500.0f;
    p.bands[1].gainDb = (round % 3 == 0) ? 2.0f : 4.0f;
    eq.setParams(p);
    eq.process(x.data(), kBlock);
    benchmark::DoNotOptimize(x.data());
  }
  report(state, kBlock);
}
BENCHMARK(BM_EqualizerMoving);

void BM_NoiseGate(benchmark::State& state) {
  tonecore::NoiseGate gate(kFs);
  auto x = voiceLike(kBlock);
  for (auto _ : state) {
    gate.process(x.data(), kBlock);
    benchmark::DoNotOptimize(x.data());
  }
  report(state, kBlock);
}
BENCHMARK(BM_NoiseGate);

void BM_Compressor(benchmark::State& state) {
  tonecore::Compressor comp(kFs);
  comp.setParams({-30.0f, 4.0f, 6.0f, 5.0f, 80.0f, 3.0f, 10.0f});  // luôn đang nén
  auto x = voiceLike(kBlock);
  for (auto _ : state) {
    comp.process(x.data(), kBlock);
    benchmark::DoNotOptimize(x.data());
  }
  report(state, kBlock);
}
BENCHMARK(BM_Compressor);

// De-esser đang giảm (ngưỡng thấp → shelf tính lại mỗi 16 mẫu: trường hợp tốn nhất).
void BM_DeEsser(benchmark::State& state) {
  tonecore::DeEsser de(kFs);
  tonecore::DeEsserParams p;
  p.thresholdDb = -90.0f;
  de.setParams(p);
  auto x = voiceLike(kBlock);
  int round = 0;
  for (auto _ : state) {
    p.thresholdDb = (round++ % 2 == 0) ? -90.0f : -80.0f;
    de.setParams(p);
    de.process(x.data(), kBlock);
    benchmark::DoNotOptimize(x.data());
  }
  report(state, kBlock);
}
BENCHMARK(BM_DeEsser);

}  // namespace
