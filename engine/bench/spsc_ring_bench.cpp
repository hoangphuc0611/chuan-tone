#include <benchmark/benchmark.h>

#include <vector>

#include "core/spsc_ring.h"

// Chi phí đẩy/lấy một block float qua ring (một luồng) – ước lượng chi phí audio thread
// khi gửi dữ liệu sang luồng ghi file.
static void BM_SpscRingPushPop(benchmark::State& state) {
  const auto block = static_cast<std::size_t>(state.range(0));
  tonecore::SpscRing<float> ring(block * 4);
  std::vector<float> src(block, 1.0f);
  std::vector<float> dst(block);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ring.push(src.data(), block));
    benchmark::DoNotOptimize(ring.pop(dst.data(), block));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * block * sizeof(float) * 2));
}
BENCHMARK(BM_SpscRingPushPop)->Arg(128)->Arg(480)->Arg(1024);
