// Tiện ích đo cho bộ FX (tests/util/fx_eval): kiểm trên hệ có đáp án.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "dsp/biquad.h"
#include "util/fx_eval.h"

using namespace tonecore;

namespace {
constexpr double kFs = 48000.0;
}  // namespace

// Đáp ứng xung → |H| tại f khớp công thức của biquad.
TEST(FxEval, MagnitudeFromImpulseResponse) {
  Biquad bq;
  bq.setCoeffs(BiquadCoeffs::peaking(kFs, 1000.0, 2.0, 6.0));
  const auto ir = test::impulseResponse([&](float* x, uint32_t n) { bq.process(x, x, n); },
                                        static_cast<std::size_t>(kFs));
  for (const double f : {100.0, 700.0, 1000.0, 1500.0, 9000.0}) {
    EXPECT_NEAR(test::magnitudeDb(ir, f, kFs),
                20.0 * std::log10(BiquadCoeffs::peaking(kFs, 1000.0, 2.0, 6.0).magnitudeAt(f, kFs)),
                0.01)
        << f;
  }
}

// Nhiễu trắng tắt dần hàm mũ với RT60 đặt trước → đo lại đúng (±3%, cả khi lọc băng 500 Hz–2 kHz).
TEST(FxEval, Rt60OfExponentialDecay) {
  for (const double t60 : {0.6, 1.4, 2.2}) {
    std::vector<float> ir(static_cast<std::size_t>((t60 * 1.5) * kFs));
    uint32_t state = 7;
    const double tau = t60 / std::log(1000.0);  // biên độ giảm 60 dB sau t60
    for (std::size_t i = 0; i < ir.size(); ++i) {
      state = state * 1664525u + 1013904223u;
      const double noise = static_cast<double>(state >> 8) / 8388608.0 - 1.0;
      ir[i] = static_cast<float>(noise * std::exp(-static_cast<double>(i) / kFs / tau));
    }
    EXPECT_NEAR(test::rt60(ir, kFs), t60, 0.03 * t60);
    EXPECT_NEAR(test::rt60(ir, kFs, 500.0, 2000.0), t60, 0.05 * t60);
  }
}

// Bộ nhân 0,5 → đường tĩnh lệch đúng −6,02 dB ở mọi mức.
TEST(FxEval, StaticCurveOfGain) {
  const auto out = test::staticCurveDb(
      [](float* x, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) x[i] *= 0.5f;
      },
      {-60.0, -30.0, -10.0, 0.0}, 1000.0, kFs, 0.3);
  const double in[] = {-60.0, -30.0, -10.0, 0.0};
  for (std::size_t i = 0; i < out.size(); ++i) EXPECT_NEAR(out[i], in[i] - 6.0206, 0.02) << i;
}
