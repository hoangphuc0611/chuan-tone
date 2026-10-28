// Delay theo nhịp và reverb FDN cho giọng (M5 B3): vọng đúng chỗ, đúng độ nhỏ dần, tối dần; RT60
// đúng, ổn định, stereo rộng, không "kim loại", mức vang không đổi theo kiểu phòng.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "fx/delay.h"
#include "fx/reverb.h"
#include "fx/svf.h"
#include "util/fx_eval.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> delayImpulse(Delay& d, std::size_t n) {
  std::vector<float> in(n, 0.0f);
  in[0] = 1.0f;
  std::vector<float> wet(n);
  for (std::size_t i = 0; i < n; i += 256) {
    const auto len = static_cast<uint32_t>(std::min<std::size_t>(256, n - i));
    d.process(in.data() + i, wet.data() + i, len);
  }
  return wet;
}

struct Stereo {
  std::vector<float> l;
  std::vector<float> r;
};

Stereo reverbImpulse(Reverb& rv, std::size_t n) {
  std::vector<float> in(n, 0.0f);
  in[0] = 1.0f;
  Stereo s{std::vector<float>(n), std::vector<float>(n)};
  for (std::size_t i = 0; i < n; i += 256) {
    const auto len = static_cast<uint32_t>(std::min<std::size_t>(256, n - i));
    rv.process(in.data() + i, s.l.data() + i, s.r.data() + i, len);
  }
  return s;
}

double energy(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double e = 0.0;
  for (std::size_t i = from; i < std::min(to, x.size()); ++i) e += static_cast<double>(x[i]) * x[i];
  return e;
}

DelayParams plainDelay(NoteDivision div, float bpm, float feedback) {
  DelayParams p;
  p.division = div;
  p.bpm = bpm;
  p.feedback = feedback;
  p.mix = 1.0f;
  p.lowPassHz = 0.0f;
  p.highPassHz = 0.0f;
  return p;
}

}  // namespace

// 92 BPM, 1/8 chấm: tiếng vọng đầu ở 60/92 × 0,75 s (lẻ mẫu → đỉnh ở mẫu gần nhất ±1).
TEST(Delay, EchoAtNoteDivision) {
  for (const auto div : {NoteDivision::Quarter, NoteDivision::Eighth, NoteDivision::DottedEighth,
                         NoteDivision::Sixteenth}) {
    Delay d(kFs);
    d.setParams(plainDelay(div, 92.0f, 0.0f));
    d.reset();
    const double want = Delay::delaySeconds(plainDelay(div, 92.0f, 0.0f)) * kFs;
    const auto wet = delayImpulse(d, static_cast<std::size_t>(want) + 1000);
    std::size_t peak = 0;
    for (std::size_t i = 0; i < wet.size(); ++i) {
      if (std::fabs(wet[i]) > std::fabs(wet[peak])) peak = i;
    }
    EXPECT_NEAR(static_cast<double>(peak), want, 1.0) << static_cast<int>(div);
    // Nội suy Lagrange bậc 3 là thông thấp nhẹ khi trễ lẻ mẫu: trong băng giọng (≤ 4 kHz) gần như
    // phẳng.
    for (const double f : {200.0, 1000.0, 4000.0}) {
      EXPECT_NEAR(test::magnitudeDb(wet, f, kFs), 0.0, 0.25) << f << " Hz";
    }
  }
}

// Mỗi lần vọng nhỏ đi đúng hệ số hồi tiếp (±0,5 dB), không có bộ lọc, trễ nguyên mẫu (không nội
// suy — nội suy kiểm ở test trên).
TEST(Delay, FeedbackDecayPerRepeat) {
  Delay d(kFs);
  const auto p = plainDelay(NoteDivision::Milliseconds, 100.0f, 0.4f);
  DelayParams q = p;
  q.timeMs = 125.0f;  // 6000 mẫu
  d.setParams(q);
  d.reset();
  const double D = d.delaySamples();
  const auto wet = delayImpulse(d, static_cast<std::size_t>(6.5 * D));
  for (int k = 1; k < 5; ++k) {
    const auto a = [&](int m) {
      const auto c = static_cast<std::size_t>(m * D);
      return energy(wet, c - 20, c + 20);
    };
    EXPECT_NEAR(10.0 * std::log10(a(k + 1) / a(k)), 20.0 * std::log10(0.4), 0.5) << k;
  }
}

// Có LPF 4 kHz trong vòng: mỗi lần vọng phần cao tần (> 4 kHz) so với tổng giảm dần.
TEST(Delay, RepeatsGetDarker) {
  Delay d(kFs);
  DelayParams p = plainDelay(NoteDivision::Milliseconds, 100.0f, 0.5f);
  p.timeMs = 200.0f;
  p.lowPassHz = 4000.0f;
  d.setParams(p);
  d.reset();
  // Vào: nhiễu 5 ms (đủ băng rộng).
  std::vector<float> in(static_cast<std::size_t>(1.2 * kFs), 0.0f);
  uint32_t state = 3;
  for (std::size_t i = 0; i < 240; ++i) {
    state = state * 1664525u + 1013904223u;
    in[i] = static_cast<float>(static_cast<double>(state >> 8) / 8388608.0 - 1.0);
  }
  std::vector<float> wet(in.size());
  d.process(in.data(), wet.data(), static_cast<uint32_t>(in.size()));
  const auto D = static_cast<std::size_t>(d.delaySamples());
  double prev = 1e9;
  for (std::size_t k = 1; k <= 4; ++k) {
    std::vector<float> seg(wet.begin() + static_cast<std::ptrdiff_t>(k * D - 100),
                           wet.begin() + static_cast<std::ptrdiff_t>(k * D + 1500));
    const double hf = test::energyAboveDb(seg, 4000.0, kFs);
    EXPECT_LT(hf, prev) << k;
    prev = hf;
  }
}

// Hồi tiếp bị chặn ở 50% (chống rú); tắt delay → không có vọng.
TEST(Delay, FeedbackCapAndOff) {
  Delay d(kFs);
  DelayParams p = plainDelay(NoteDivision::Milliseconds, 100.0f, 0.9f);
  p.timeMs = 50.0f;
  d.setParams(p);
  d.reset();
  const auto wet = delayImpulse(d, static_cast<std::size_t>(0.2 * kFs));
  const auto at = [&](int k) { return energy(wet, k * 2400 - 20, k * 2400 + 20); };
  EXPECT_NEAR(10.0 * std::log10(at(2) / at(1)), 20.0 * std::log10(0.5), 0.5);

  Delay off(kFs);
  DelayParams o;
  o.division = NoteDivision::Off;
  off.setParams(o);
  off.reset();
  const auto none = delayImpulse(off, 20000);
  EXPECT_EQ(energy(none, 0, none.size()), 0.0);
}

// RT60 đo trên đáp ứng xung (băng 500 Hz–2 kHz, Schroeder T30) khớp giá trị đặt ±15%, cả hai kênh.
TEST(Reverb, Rt60MatchesSetting) {
  struct Case {
    ReverbType type;
    float decay;
  };
  for (const auto c :
       {Case{ReverbType::Room, 0.6f}, Case{ReverbType::Room, 1.2f}, Case{ReverbType::Hall, 2.2f},
        Case{ReverbType::Plate, 1.4f}, Case{ReverbType::Plate, 1.8f}}) {
    Reverb rv(kFs);
    ReverbParams p;
    p.type = c.type;
    p.decaySec = c.decay;
    p.damping = 0.5f;
    p.preDelayMs = 0.0f;
    p.mix = 1.0f;
    rv.setParams(p);
    rv.reset();
    const auto ir = reverbImpulse(
        rv, static_cast<std::size_t>((1.5 * static_cast<double>(c.decay) + 0.3) * kFs));
    for (const auto* ch : {&ir.l, &ir.r}) {
      EXPECT_NEAR(test::rt60(*ch, kFs, 500.0, 2000.0), static_cast<double>(c.decay),
                  0.15 * static_cast<double>(c.decay))
          << static_cast<int>(c.type) << " " << c.decay;
    }
  }
}

// Damping: RT60 ở 4–8 kHz ngắn hơn ở 500 Hz–2 kHz (vang tối dần như phòng thật).
TEST(Reverb, DampingShortensHighFrequencyDecay) {
  Reverb rv(kFs);
  ReverbParams p;
  p.decaySec = 2.0f;
  p.damping = 0.6f;
  p.preDelayMs = 0.0f;
  p.mix = 1.0f;
  rv.setParams(p);
  rv.reset();
  const auto ir = reverbImpulse(rv, static_cast<std::size_t>(3.3 * kFs));
  EXPECT_LT(test::rt60(ir.l, kFs, 4000.0, 8000.0), 0.8 * test::rt60(ir.l, kFs, 500.0, 2000.0));
}

// Ổn định: sau 100 ms năng lượng từng đoạn 50 ms không tăng (+0,5 dB dung sai), tắt hẳn về sau.
TEST(Reverb, DecaysMonotonically) {
  Reverb rv(kFs);
  ReverbParams p;
  p.type = ReverbType::Hall;
  p.decaySec = 2.2f;
  p.mix = 1.0f;
  rv.setParams(p);
  rv.reset();
  const auto ir = reverbImpulse(rv, static_cast<std::size_t>(4.0 * kFs));
  const auto win = static_cast<std::size_t>(0.05 * kFs);
  double prev = 1e300;
  for (std::size_t s = static_cast<std::size_t>(0.15 * kFs); s + win <= ir.l.size(); s += win) {
    const double e = energy(ir.l, s, s + win) + energy(ir.r, s, s + win);
    EXPECT_LT(10.0 * std::log10(e / prev), 0.5) << s;
    prev = e;
  }
  for (const float v : ir.l) ASSERT_TRUE(std::isfinite(v));
}

// Stereo rộng: tương quan L–R của đuôi (sau 50 ms) ≤ 0,3 ở mọi kiểu phòng.
TEST(Reverb, WideStereoTail) {
  for (const auto type : {ReverbType::Room, ReverbType::Hall, ReverbType::Plate}) {
    Reverb rv(kFs);
    ReverbParams p;
    p.type = type;
    p.preDelayMs = 0.0f;
    p.mix = 1.0f;
    rv.setParams(p);
    rv.reset();
    const auto ir = reverbImpulse(rv, static_cast<std::size_t>(1.5 * kFs));
    EXPECT_LT(
        std::fabs(test::correlation(ir.l, ir.r, static_cast<std::size_t>(0.05 * kFs), ir.l.size())),
        0.3)
        << static_cast<int>(type);
  }
}

// Không "kim loại" (mode ngân nổi trội — hiện tượng của đuôi dài): ở các RT60 preset dùng (≥ 1 s),
// phổ đuôi (Welch, mỗi đoạn chuẩn hoá) không có mode nào nhô quá 8 dB so với vùng ±1/6 quãng tám,
// 200 Hz–8 kHz. (RT 0,6 s: 2–8 kHz nhô ~10 dB do vài tiếng dội rời trước khi mạng kịp trộn —
// docs/fx-results.md, nghe kiểm ở B6.)
TEST(Reverb, NoMetallicResonances) {
  struct Case {
    ReverbType type;
    float decay;
  };
  for (const auto c :
       {Case{ReverbType::Room, 1.0f}, Case{ReverbType::Room, 1.2f}, Case{ReverbType::Hall, 2.2f},
        Case{ReverbType::Plate, 1.4f}, Case{ReverbType::Plate, 1.8f}}) {
    Reverb rv(kFs);
    ReverbParams p;
    p.type = c.type;
    p.decaySec = c.decay;
    p.preDelayMs = 0.0f;
    p.mix = 1.0f;
    rv.setParams(p);
    rv.reset();
    const auto ir = reverbImpulse(
        rv, static_cast<std::size_t>((1.5 * static_cast<double>(c.decay) + 0.3) * kFs));
    for (const auto* ch : {&ir.l, &ir.r}) {
      EXPECT_LT(test::spectralPeakinessDb(*ch, kFs, static_cast<std::size_t>(0.1 * kFs), ch->size(),
                                          200.0, 8000.0),
                8.0)
          << static_cast<int>(c.type) << " " << c.decay;
    }
  }
}

// "mix" là tỉ lệ biên độ vang / khô TRONG BĂNG GIỌNG (300 Hz–2 kHz): năng lượng băng đó của đáp
// ứng xung mỗi kênh ≈ mix² × năng lượng băng đó của xung vào (±3 dB), bất kể kiểu phòng và RT60.
// (Cả dải thì thấp hơn vì vang tắt nhanh ở cao tần — damping.) Pre-delay dời đúng tiếng vang đầu.
TEST(Reverb, LevelIndependentOfTypeAndDecay) {
  const auto voiceBand = [](std::vector<float> x) {
    const SvfCoeffs hp = SvfCoeffs::make(SvfType::HighPass, kFs, 300.0, 0.7071, 0.0);
    const SvfCoeffs lp = SvfCoeffs::make(SvfType::LowPass, kFs, 2000.0, 0.7071, 0.0);
    for (int pass = 0; pass < 2; ++pass) {
      Svf a, b;
      for (float& v : x) v = b.process(a.process(v, hp), lp);
    }
    return x;
  };
  for (const auto type : {ReverbType::Room, ReverbType::Hall, ReverbType::Plate}) {
    for (const float decay : {0.6f, 2.2f}) {
      Reverb rv(kFs);
      ReverbParams p;
      p.type = type;
      p.decaySec = decay;
      p.preDelayMs = 25.0f;
      p.mix = 0.5f;
      rv.setParams(p);
      rv.reset();
      const auto ir = reverbImpulse(rv, static_cast<std::size_t>((1.5 * decay + 0.3) * kFs));
      std::vector<float> imp(ir.l.size(), 0.0f);
      imp[0] = 1.0f;
      const double ref = energy(voiceBand(imp), 0, imp.size());
      for (const auto* ch : {&ir.l, &ir.r}) {
        EXPECT_NEAR(10.0 * std::log10(energy(voiceBand(*ch), 0, ch->size()) / ref),
                    20.0 * std::log10(0.5), 3.0)
            << static_cast<int>(type) << " " << decay;
      }
      const auto pre = static_cast<std::size_t>(0.025 * kFs);
      EXPECT_EQ(energy(ir.l, 0, pre), 0.0);
    }
  }
}
