// Giá đỡ chuỗi FX và đổi preset không click (M5 B5): preset "Khô" đi thẳng; đổi thông số liên tục
// không dựng lại chuỗi; đổi cấu trúc thì phần khô crossfade 15 ms và đuôi vang cũ ngân nốt; đổi
// liên tục không click, không hụt tiếng; bấm dồn chỉ giữ lần cuối; an toàn khi hai luồng chạy song
// song.

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "fx/fx_presets.h"
#include "fx/fx_rack.h"
#include "util/fx_eval.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;
constexpr uint32_t kBlock = 128;
constexpr double kPi = 3.14159265358979323846;

struct Out {
  std::vector<float> l;
  std::vector<float> r;
};

// Giọng tổng hợp: sin 220 Hz (−12 dBFS) trong [0, voiceSec), im lặng sau đó. `hook(block)` gọi
// trước mỗi khối (đổi preset ở luồng điều khiển giữa các khối).
template <typename Hook>
Out run(FxRack& rack, double seconds, double voiceSec, Hook hook) {
  const auto n = static_cast<std::size_t>(seconds * kFs);
  Out o{std::vector<float>(n), std::vector<float>(n)};
  std::vector<float> v(kBlock);
  for (std::size_t s = 0, b = 0; s < n; s += kBlock, ++b) {
    hook(b);
    const auto len = static_cast<uint32_t>(std::min<std::size_t>(kBlock, n - s));
    for (uint32_t i = 0; i < len; ++i) {
      const double t = static_cast<double>(s + i) / kFs;
      v[i] = t < voiceSec ? static_cast<float>(0.25 * std::sin(2.0 * kPi * 220.0 * t)) : 0.0f;
    }
    rack.process(v.data(), o.l.data() + s, o.r.data() + s, len);
  }
  return o;
}

// Bolero bỏ delay: đuôi chỉ còn reverb (tiếng vọng mono của delay sẽ át phần vang stereo).
FxPreset reverbOnly() {
  FxPreset p = genrePreset(Genre::Bolero);
  p.delay.division = NoteDivision::Off;
  return p;
}

double energyDb(const Out& o, double from, double to) {
  double e = 0.0;
  for (auto i = static_cast<std::size_t>(from * kFs); i < static_cast<std::size_t>(to * kFs); ++i) {
    e += static_cast<double>(o.l[i]) * o.l[i] + static_cast<double>(o.r[i]) * o.r[i];
  }
  return 10.0 * std::log10(e + 1e-30);
}

}  // namespace

// Mặc định "Khô": ra hai kênh = giọng vào, đúng từng mẫu.
TEST(FxRack, DryIsBitExact) {
  FxRack rack(kFs, kBlock);
  const auto o = run(rack, 0.5, 0.5, [](std::size_t) {});
  for (std::size_t i = 0; i < o.l.size(); ++i) {
    const double t = static_cast<double>(i) / kFs;
    const auto want = static_cast<float>(0.25 * std::sin(2.0 * kPi * 220.0 * t));
    ASSERT_EQ(o.l[i], want) << i;
    ASSERT_EQ(o.r[i], want) << i;
  }
  EXPECT_EQ(rack.audioChains(), 1);
}

// Bolero: sau khi hát xong vẫn còn đuôi vang rõ (−3…−40 dB so với lúc hát); phần vang (bỏ delay —
// tiếng vọng mono của delay sẽ át) hai kênh gần như độc lập.
TEST(FxRack, PresetRingsAfterVoiceStops) {
  for (const bool withDelay : {true, false}) {
    FxRack rack(kFs, kBlock);
    rack.apply(withDelay ? genrePreset(Genre::Bolero) : reverbOnly());
    const auto o = run(rack, 2.0, 1.0, [](std::size_t) {});
    const double during = energyDb(o, 0.5, 1.0);
    const double after = energyDb(o, 1.2, 1.7);
    EXPECT_GT(after, during - 40.0) << withDelay;
    EXPECT_LT(after, during - 3.0) << withDelay;
    if (!withDelay) {
      EXPECT_LT(std::fabs(test::correlation(o.l, o.r, static_cast<std::size_t>(1.2 * kFs),
                                            static_cast<std::size_t>(1.7 * kFs))),
                0.3);
    }
  }
}

// Đổi độ ướt / EQ (cùng cấu trúc) khi đang hát: không dựng chuỗi mới (vẫn 1 chuỗi sau lần đổi),
// đuôi vang to lên đúng +6 dB khi độ ướt gấp đôi.
TEST(FxRack, ContinuousChangeUpdatesInPlace) {
  const FxPreset p = reverbOnly();
  FxRack quietRack(kFs, kBlock);
  quietRack.apply(p);
  const auto quiet = run(quietRack, 2.0, 1.0, [](std::size_t) {});
  FxRack rack(kFs, kBlock);
  rack.apply(p);
  int maxChains = 0;
  const auto loud = run(rack, 2.0, 1.0, [&](std::size_t b) {
    if (b == 100) {
      FxPreset q = p;
      q.reverb.mix = 2.0f * p.reverb.mix;
      q.eq.bands[1].gainDb = 4.0f;
      rack.apply(q);
    }
    if (b > 100) maxChains = std::max(maxChains, rack.audioChains());
  });
  EXPECT_EQ(maxChains, 1);
  EXPECT_NEAR(energyDb(loud, 1.3, 1.8) - energyDb(quiet, 1.3, 1.8), 6.02, 1.0);
}

// Đổi sang "Khô" đúng lúc hát xong: đuôi Hall của preset cũ vẫn ngân (không tắt cụt trong 15 ms),
// có 2 chuỗi trong lúc ngân, rồi chuỗi cũ được trả về khi đuôi < −80 dB (hoặc quá 4 s).
TEST(FxRack, StructuralChangeLetsOldTailRingOut) {
  FxRack rack(kFs, kBlock);
  rack.apply(genrePreset(Genre::Bolero));
  const std::size_t swapBlock = static_cast<std::size_t>(1.0 * kFs) / kBlock;
  int chainsAfterSwap = 0;
  const auto o = run(rack, 6.0, 1.0, [&](std::size_t b) {
    if (b == swapBlock) rack.apply(FxPreset{});
    if (b == swapBlock + 40) chainsAfterSwap = rack.audioChains();
    if (b % 50 == 0) rack.collectGarbage();
  });
  EXPECT_EQ(chainsAfterSwap, 2);
  const double during = energyDb(o, 0.5, 1.0);
  EXPECT_GT(energyDb(o, 1.2, 1.5), during - 40.0);  // còn ngân 200–500 ms sau khi đổi
  EXPECT_EQ(rack.audioChains(), 1);                 // đã trả về (≤ 4 s)
  EXPECT_LT(energyDb(o, 5.5, 6.0), during - 80.0);
}

// Đổi preset liên tục (mỗi 200 ms, qua đủ 7 preset) khi đang hát sin 220 Hz: không click (năng
// lượng trên 12 kHz ≤ −60 dB so với tổng) và không hụt tiếng (năng lượng 10 ms quanh mỗi lần đổi ≥
// mức thấp hơn trong hai bên − 3 dB).
TEST(FxRack, SwapsAreClickFreeWithoutDropouts) {
  FxRack rack(kFs, kBlock);
  const Genre order[] = {Genre::Bolero, Genre::Dry, Genre::Ballad, Genre::Pop,
                         Genre::Remix,  Genre::Rap, Genre::Lofi,   Genre::Bolero,
                         Genre::Pop,    Genre::Dry, Genre::Lofi,   Genre::Ballad};
  const std::size_t every = static_cast<std::size_t>(0.2 * kFs) / kBlock;
  const auto o = run(rack, 2.6, 2.6, [&](std::size_t b) {
    if (b % every == 0 && b / every < std::size(order)) rack.apply(genrePreset(order[b / every]));
    rack.collectGarbage();
  });
  std::vector<float> mono(o.l.size());
  for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (o.l[i] + o.r[i]);
  EXPECT_LT(test::energyAboveDb(mono, 12000.0, kFs), -60.0);
  // Mức mỗi 10 ms quanh lần đổi so với mức (quy về 10 ms) của 40 ms trước / sau.
  for (std::size_t k = 1; k < std::size(order); ++k) {
    const double t = static_cast<double>(k * every * kBlock) / kFs;
    const double per10 = 10.0 * std::log10(0.25);  // 40 ms → 10 ms
    const double before = energyDb(o, t - 0.05, t - 0.01) + per10;
    const double after = energyDb(o, t + 0.05, t + 0.09) + per10;
    for (double w = t - 0.005; w < t + 0.03; w += 0.005) {
      EXPECT_GT(energyDb(o, w, w + 0.01), std::min(before, after) - 3.0)
          << "lần đổi " << k << " @ " << w;
    }
  }
}

// Bấm dồn nhiều preset trước khi audio thread kịp nhận: chỉ preset cuối được dùng (1 chuỗi mới +
// chuỗi cũ đang ngân), các chuỗi giữa chừng huỷ ngay ở luồng điều khiển.
TEST(FxRack, RapidAppliesCoalesce) {
  FxRack rack(kFs, kBlock);
  for (int k = 0; k < 20; ++k) rack.apply(genrePreset(static_cast<Genre>(1 + k % 6)));
  run(rack, 0.05, 0.05, [](std::size_t) {});
  EXPECT_LE(rack.audioChains(), 2);
  EXPECT_EQ(rack.preset().reverb.type, genrePreset(static_cast<Genre>(1 + 19 % 6)).reverb.type);
}

// "Vang" = 0: hết đuôi ngay sau khi hát; = 2: đuôi to gấp đôi (+6 dB) so với 1.
TEST(FxRack, WetScale) {
  const auto tail = [](float wet) {
    FxRack rack(kFs, kBlock);
    rack.apply(genrePreset(Genre::Bolero));
    rack.setWet(wet);
    const auto o = run(rack, 1.8, 1.0, [](std::size_t) {});
    return energyDb(o, 1.2, 1.7);
  };
  const double one = tail(1.0f);
  EXPECT_NEAR(tail(2.0f) - one, 6.02, 0.3);
  EXPECT_LT(tail(0.0f), one - 60.0);
}

// Tắt FX = chuỗi "Khô" (đuôi cũ ngân nốt); bật lại = preset đã đặt. Đổi tempo khi có delay theo
// nhịp → dựng chuỗi mới (thời gian vọng đổi).
TEST(FxRack, EnableAndTempo) {
  FxRack rack(kFs, kBlock);
  rack.apply(genrePreset(Genre::Bolero));
  run(rack, 0.2, 0.2, [](std::size_t) {});
  rack.setEnabled(false);
  run(rack, 0.05, 0.05, [](std::size_t) {});
  EXPECT_EQ(rack.audioChains(), 2);  // "Khô" + Bolero đang ngân
  EXPECT_FALSE(rack.enabled());
  EXPECT_EQ(rack.preset().reverb.type, ReverbType::Hall);  // preset người dùng vẫn nhớ
  rack.setEnabled(true);
  rack.setTempo(120.0f);
  EXPECT_EQ(rack.preset().delay.bpm, 120.0f);
}

// Luồng điều khiển đổi preset / "Vang" / tempo liên tục trong khi audio thread xử lý (TSan bắt
// race).
TEST(FxRack, ConcurrentControlAndAudio) {
  FxRack rack(kFs, kBlock);
  std::atomic<bool> stop{false};
  std::thread control([&] {
    for (int k = 0; !stop.load(); ++k) {
      rack.apply(genrePreset(static_cast<Genre>(k % 7)));
      rack.setWet(0.5f + 0.1f * static_cast<float>(k % 10));
      if (k % 5 == 0) rack.setTempo(80.0f + static_cast<float>(k % 40));
      rack.collectGarbage();
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
  });
  std::vector<float> v(kBlock, 0.1f), l(kBlock), r(kBlock);
  for (int b = 0; b < 2000; ++b) {
    rack.process(v.data(), l.data(), r.data(), kBlock);
    for (uint32_t i = 0; i < kBlock; ++i) ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
  }
  stop.store(true);
  control.join();
  rack.collectGarbage();
}
