// Cổng chặn tiếng ồn cho giọng (M5 B1): mở nhanh, không nháy quanh ngưỡng, giữ rồi nhả, khi đóng
// chỉ giảm đúng −30 dB.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "fx/noise_gate.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Sin 1 kHz, biên độ đỉnh theo dBFS từng mẫu: level(t).
template <typename F>
std::vector<float> tone(double seconds, F levelDb) {
  std::vector<float> x(static_cast<std::size_t>(seconds * kFs));
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double t = static_cast<double>(i) / kFs;
    const double l = levelDb(t);
    x[i] = l <= -200.0
               ? 0.0f
               : static_cast<float>(std::pow(10.0, l / 20.0) * std::sin(2.0 * kPi * 1000.0 * t));
  }
  return x;
}

// Chạy gate theo khối 128 mẫu, ghi hệ số và trạng thái từng khối.
struct Trace {
  std::vector<float> gain;  // mỗi mẫu
  std::vector<bool> open;
};
Trace run(NoiseGate& g, std::vector<float>& x) {
  Trace t;
  for (std::size_t i = 0; i < x.size(); i += 128) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(128, x.size() - i));
    for (uint32_t k = 0; k < n; ++k) {
      g.process(x.data() + i + k, 1);
      t.gain.push_back(g.gain());
      t.open.push_back(g.isOpen());
    }
  }
  return t;
}

double db(double v) {
  return 20.0 * std::log10(v);
}

}  // namespace

// Đang im → tiếng to hơn ngưỡng 10 dB: hệ số lên tới −1 dB trong ≤ 3 ms (attack 1 ms).
TEST(NoiseGate, OpensFast) {
  NoiseGate g(kFs);
  auto x = tone(0.3, [](double t) { return t < 0.1 ? -300.0 : -40.0; });
  const Trace tr = run(g, x);
  const auto start = static_cast<std::size_t>(0.1 * kFs);
  std::size_t at = start;
  while (at < tr.gain.size() && db(tr.gain[at]) < -1.0) ++at;
  EXPECT_LE(static_cast<double>(at - start) / kFs, 0.003);
}

// Tiếng dưới ngưỡng (−60 dBFS, ngưỡng −50) → cổng đóng, ra = vào × (−30 dB) đúng.
TEST(NoiseGate, ClosedAttenuatesByRange) {
  NoiseGate g(kFs);
  auto x = tone(1.0, [](double) { return -60.0; });
  const auto in = x;
  const Trace tr = run(g, x);
  for (std::size_t i = x.size() / 2; i < x.size(); ++i) {
    ASSERT_FALSE(tr.open[i]);
    ASSERT_NEAR(x[i], in[i] * std::pow(10.0f, -30.0f / 20.0f), 1e-6f) << i;
  }
}

// Tiếng dao động quanh ngưỡng ±2 dB (3 Hz, 2 s) sau khi đã mở: không đóng lại lần nào (trễ 6 dB).
TEST(NoiseGate, NoChatterAroundThreshold) {
  NoiseGate g(kFs);
  auto x = tone(2.2, [](double t) {
    return t < 0.2 ? -49.0 : -50.0 + 2.0 * std::sin(2.0 * kPi * 3.0 * (t - 0.2));
  });
  const Trace tr = run(g, x);
  int changes = 0;
  for (std::size_t i = 1; i < tr.open.size(); ++i) changes += tr.open[i] != tr.open[i - 1];
  EXPECT_EQ(changes, 1);  // chỉ lần mở đầu tiên
  EXPECT_TRUE(tr.open.back());
}

// Tắt tiếng đột ngột: còn mở trọn trong khi bao biên chưa xuống dưới ngưỡng đóng + thời gian giữ
// (80 ms), rồi nhả về −30 dB theo τ release 150 ms.
TEST(NoiseGate, HoldsThenReleases) {
  NoiseGate g(kFs);
  constexpr double kStop = 0.5;
  auto x = tone(2.0, [&](double t) { return t < kStop ? -40.0 : -300.0; });
  const Trace tr = run(g, x);
  // Bao biên giảm từ −40 dB (đỉnh) tới ngưỡng đóng −56 dB: τ_detector · ln(10^(16/20)).
  const double fall = NoiseGate::kDetectorSec * std::log(std::pow(10.0, 16.0 / 20.0));
  const double closeAt = kStop + fall + 0.080;
  const auto idx = [](double t) { return static_cast<std::size_t>(t * kFs); };
  EXPECT_TRUE(tr.open[idx(closeAt - 0.003)]);
  EXPECT_FALSE(tr.open[idx(closeAt + 0.003)]);
  EXPECT_GT(tr.gain[idx(closeAt - 0.003)], 0.999f);
  // Nhả một cực trong miền tuyến tính: g(t) = sàn + (1 − sàn)·e^(−t/τ), τ = 150 ms.
  const double floor = std::pow(10.0, -30.0 / 20.0);
  for (const double k : {1.0, 3.0}) {
    EXPECT_NEAR(tr.gain[idx(closeAt + k * 0.150)], floor + (1.0 - floor) * std::exp(-k), 0.01)
        << k << "τ";
  }
  EXPECT_NEAR(db(tr.gain.back()), -30.0, 0.1);  // ~4τ sau: đã về sàn
}
