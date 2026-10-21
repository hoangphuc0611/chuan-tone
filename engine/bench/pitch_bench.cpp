// Chi phí CPU của khối Tự chỉnh giọng, báo theo "% một nhân" (rt_factor nghịch đảo).
// Tín hiệu: giọng giả (tổng hoà âm 220 Hz có rung giọng) → luôn hữu thanh, PSOLA luôn chạy:
// trường hợp tốn nhất.

#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

#include "core/engine.h"
#include "pitch/voice_pitch_node.h"

namespace {

std::vector<float> fakeVoice(std::size_t n) {
  std::vector<float> v(n);
  double phase = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    const double f0 = 220.0 * std::pow(2.0, 0.4 / 12.0 * std::sin(2 * 3.14159265 * 5.5 * t));
    phase += f0 / 48000.0;
    double s = 0.0;
    for (int h = 1; h <= 12; ++h) s += std::sin(2 * 3.14159265 * h * phase) / h;
    v[i] = static_cast<float>(0.2 * s);
  }
  return v;
}

void BM_VoicePitchNode(benchmark::State& state) {
  const auto frames = static_cast<uint32_t>(state.range(0));
  tonecore::VoicePitchNode node(48000.0, 1024);
  node.setKey(0, tonecore::Scale::Major);
  node.applyPreset(tonecore::CorrectionPreset::Singer);
  node.setEnabled(true);
  const auto voice = fakeVoice(48000 * 4);
  std::vector<float> buf(frames);
  std::size_t pos = 0;
  for (auto _ : state) {
    if (pos + frames > voice.size()) pos = 0;
    std::copy(voice.begin() + static_cast<std::ptrdiff_t>(pos),
              voice.begin() + static_cast<std::ptrdiff_t>(pos + frames), buf.begin());
    node.process(buf.data(), frames);
    benchmark::DoNotOptimize(buf.data());
    pos += frames;
  }
  state.counters["rt_factor"] = benchmark::Counter(static_cast<double>(frames) / 48000.0,
                                                   benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_VoicePitchNode)->Arg(128)->Arg(192)->Arg(256)->Arg(480);

void BM_EngineWithPitch(benchmark::State& state) {
  const auto frames = static_cast<uint32_t>(state.range(0));
  tonecore::EngineConfig cfg;
  cfg.maxBlockFrames = frames;
  tonecore::Engine engine(cfg);
  engine.start();
  engine.setKey(0, TC_SCALE_MAJOR);
  engine.setParam(TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f);
  engine.setParam(TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f);
  const auto voice = fakeVoice(48000 * 4);
  std::vector<float> out(frames * cfg.outputChannels);
  std::size_t pos = 0;
  for (auto _ : state) {
    if (pos + frames > voice.size()) pos = 0;
    engine.process(voice.data() + pos, out.data(), frames);
    benchmark::DoNotOptimize(out.data());
    pos += frames;
  }
  state.counters["rt_factor"] = benchmark::Counter(static_cast<double>(frames) / 48000.0,
                                                   benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_EngineWithPitch)->Arg(128)->Arg(256);

}  // namespace
