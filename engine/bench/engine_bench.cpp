// Đo chi phí CPU của Engine::process cho các cỡ block thường gặp trên máy thật.
// Báo cáo "% một nhân" = thời gian xử lý / thời lượng audio của block.

#include <benchmark/benchmark.h>

#include <vector>

#include "core/engine.h"

static void BM_EngineProcess(benchmark::State& state) {
  const auto frames = static_cast<uint32_t>(state.range(0));
  tonecore::EngineConfig cfg;
  cfg.maxBlockFrames = frames;
  tonecore::Engine engine(cfg);
  engine.start();

  std::vector<float> in(frames, 0.25f);
  std::vector<float> out(frames * cfg.outputChannels);
  engine.setParam(TC_NODE_MASTER, TC_PARAM_GAIN, 0.5f);

  for (auto _ : state) {
    engine.process(in.data(), out.data(), frames);
    benchmark::DoNotOptimize(out.data());
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * frames);
  // Số giây audio xử lý được mỗi giây CPU → nghịch đảo là tỉ lệ tải một nhân.
  state.counters["rt_factor"] = benchmark::Counter(static_cast<double>(frames) / cfg.sampleRate,
                                                   benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_EngineProcess)->Arg(64)->Arg(128)->Arg(192)->Arg(256)->Arg(480);
