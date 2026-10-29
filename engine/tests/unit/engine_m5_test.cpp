// Engine M5 qua C API: limiter đầu ra thay chỗ kẹp ±1.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include "test_util.h"
#include "tonecore/tonecore.h"

namespace {

constexpr uint32_t kBlock = 256;
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

}  // namespace

// Beat sát 0 dBFS + giọng nghe lại to + master ×2: tổng tới ~3,8 nhưng đầu ra không mẫu nào vượt
// trần 0,98 (+1%) và không bị kẹp ±1; vẫn to (limiter chỉ hạ đỉnh). Trước M5 tầng ra kẹp ±1 → méo.
TEST(EngineLimiter, LoudMixStaysBelowCeiling) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const std::string path = tonecore::test::tempPath("m5_loud_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(
      path, tonecore::test::sineInt16(220.0, 48000, 48000 * 3, 2, 0.95), 48000, 2));
  ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
  for (int i = 0; i < 3000; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    bool loaded = false;
    for (int32_t k = 0; k < n; ++k) loaded |= ev[k].type == TC_EVENT_BEAT_LOADED;
    if (loaded) break;
    std::vector<float> idle(2 * kBlock);
    tc_engine_process_offline(e, nullptr, idle.data(), kBlock);
  }
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MASTER, TC_PARAM_GAIN, 2.0f), TC_OK);
  ASSERT_EQ(tc_play(e), TC_OK);
  std::vector<float> in(kBlock), out(2 * kBlock);
  float peak = 0.0f;
  int atUnity = 0;
  uint64_t t = 0;
  for (int b = 0; b < 300; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i, ++t) {
      in[i] =
          static_cast<float>(0.9 * std::sin(2.0 * kPi * 330.0 * static_cast<double>(t) / kRate));
    }
    ASSERT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
    if (b < 20) continue;  // bỏ lúc gain đang trượt
    for (const float v : out) {
      peak = std::max(peak, std::fabs(v));
      atUnity += std::fabs(v) >= 1.0f;
    }
  }
  EXPECT_LE(peak, 0.98f * 1.01f);
  EXPECT_GT(peak, 0.9f);
  EXPECT_EQ(atUnity, 0);
  tc_engine_destroy(e);
}

namespace {

struct FxFixture {
  tc_engine* e = nullptr;
  FxFixture() {
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
    EXPECT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  }
  ~FxFixture() { tc_engine_destroy(e); }

  // Micro: sin 220 Hz biên độ `amp` trong [0, voiceSec), im sau đó. Trả đầu ra stereo interleaved.
  std::vector<float> run(double seconds, double voiceSec, double amp = 0.25) {
    const auto n = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> all;
    std::vector<float> in(kBlock), out(2 * kBlock);
    for (std::size_t s = 0; s < n; s += kBlock) {
      for (uint32_t i = 0; i < kBlock; ++i) {
        const double t = static_cast<double>(s + i) / kRate;
        in[i] = t < voiceSec ? static_cast<float>(amp * std::sin(2.0 * kPi * 220.0 * t)) : 0.0f;
      }
      EXPECT_EQ(tc_engine_process_offline(e, in.data(), out.data(), kBlock), TC_OK);
      all.insert(all.end(), out.begin(), out.end());
      tc_event ev[8];
      tc_poll_events(e, ev, 8);  // như app: dọn chuỗi FX cũ
    }
    return all;
  }

  tc_metrics metrics() {
    tc_metrics m{};
    m.struct_size = sizeof(m);
    EXPECT_EQ(tc_get_metrics(e, &m), TC_OK);
    return m;
  }
};

double energyDb(const std::vector<float>& stereo, double from, double to) {
  double sum = 0.0;
  for (auto i = static_cast<std::size_t>(2 * from * kRate);
       i < static_cast<std::size_t>(2 * to * kRate) && i < stereo.size(); ++i) {
    sum += static_cast<double>(stereo[i]) * stereo[i];
  }
  return 10.0 * std::log10(sum + 1e-30);
}

}  // namespace

// tc_fx_preset_init → tc_apply_fx_preset → tc_get_fx_preset trả đúng preset (mọi thể loại).
TEST(EngineFx, PresetInitApplyRoundTrip) {
  FxFixture f;
  for (int32_t g = TC_GENRE_DRY; g <= TC_GENRE_LOFI; ++g) {
    tc_fx_preset p;
    tc_fx_preset_init(&p, g);
    ASSERT_EQ(p.struct_size, sizeof(tc_fx_preset));
    ASSERT_EQ(tc_apply_fx_preset(f.e, &p), TC_OK);
    tc_fx_preset back{};
    back.struct_size = sizeof(back);
    ASSERT_EQ(tc_get_fx_preset(f.e, &back), TC_OK);
    EXPECT_EQ(std::memcmp(&p, &back, sizeof(p)), 0) << g;
  }
  tc_fx_preset dry;
  tc_fx_preset_init(&dry, 99);  // thể loại lạ → Khô
  EXPECT_EQ(dry.reverb_enabled, 0);
  EXPECT_EQ(dry.delay_division, TC_DELAY_OFF);
}

// Giọng nghe lại qua preset Bolero: sau khi hát xong còn đuôi vang (Hall); preset Khô thì im ngay.
// Số đo: fx_chains = 1 khi đứng yên, fx_load > 0.
TEST(EngineFx, BoleroTailOnMonitoredVoice) {
  for (const int32_t genre : {TC_GENRE_BOLERO, TC_GENRE_DRY}) {
    FxFixture f;
    tc_fx_preset p;
    tc_fx_preset_init(&p, genre);
    ASSERT_EQ(tc_apply_fx_preset(f.e, &p), TC_OK);
    const auto out = f.run(2.0, 1.0);
    const double during = energyDb(out, 0.5, 1.0);
    const double after = energyDb(out, 1.2, 1.7);
    if (genre == TC_GENRE_BOLERO) {
      EXPECT_GT(after, during - 40.0);
      const tc_metrics m = f.metrics();
      EXPECT_EQ(m.fx_chains, 1);
      EXPECT_GT(m.fx_load, 0.0f);
    } else {
      EXPECT_LT(after, during - 100.0);
    }
  }
}

// TC_NODE_FX: tắt = Khô (đuôi Bolero vẫn ngân → 2 chuỗi), "Vang" đọc / ghi được và bị kẹp 0..2.
TEST(EngineFx, EnableAndVangParams) {
  FxFixture f;
  tc_fx_preset p;
  tc_fx_preset_init(&p, TC_GENRE_BOLERO);
  ASSERT_EQ(tc_apply_fx_preset(f.e, &p), TC_OK);
  f.run(0.5, 0.5);
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_FX, TC_PARAM_ENABLED, 0.0f), TC_OK);
  f.run(0.05, 0.05);
  EXPECT_EQ(f.metrics().fx_chains, 2);
  float v = -1.0f;
  ASSERT_EQ(tc_get_param(f.e, TC_NODE_FX, TC_PARAM_ENABLED, &v), TC_OK);
  EXPECT_EQ(v, 0.0f);
  ASSERT_EQ(tc_set_param(f.e, TC_NODE_FX, TC_PARAM_MIX, 5.0f), TC_OK);
  ASSERT_EQ(tc_get_param(f.e, TC_NODE_FX, TC_PARAM_MIX, &v), TC_OK);
  EXPECT_EQ(v, 2.0f);
  EXPECT_EQ(tc_set_param(f.e, TC_NODE_FX, TC_PARAM_GAIN, 1.0f), TC_ERR_INVALID_ARG);
}

// Gate (ngưỡng −40 dBFS): tiếng micro nhỏ (−60 dBFS) bị hạ ~30 dB; tiếng to (−12 dBFS) đi qua.
TEST(EngineFx, GateAttenuatesQuietMic) {
  const auto level = [](bool gate, double amp) {
    FxFixture f;
    if (gate) {
      EXPECT_EQ(tc_set_param(f.e, TC_NODE_GATE, TC_PARAM_ENABLED, 1.0f), TC_OK);
      EXPECT_EQ(tc_set_param(f.e, TC_NODE_GATE, TC_PARAM_THRESHOLD_DB, -40.0f), TC_OK);
    }
    const auto out = f.run(1.5, 1.5, amp);
    return energyDb(out, 1.0, 1.5);
  };
  EXPECT_NEAR(level(true, 0.001) - level(false, 0.001), -30.0, 1.0);
  EXPECT_NEAR(level(true, 0.25) - level(false, 0.25), 0.0, 0.1);
}

// Preset sai: NaN hoặc struct_size khác → TC_ERR_INVALID_ARG; ngoài khoảng thì bị kẹp.
TEST(EngineFx, RejectsBadPresetsAndClamps) {
  FxFixture f;
  tc_fx_preset p;
  tc_fx_preset_init(&p, TC_GENRE_POP);
  tc_fx_preset bad = p;
  bad.reverb_mix = NAN;
  EXPECT_EQ(tc_apply_fx_preset(f.e, &bad), TC_ERR_INVALID_ARG);
  bad = p;
  bad.struct_size = 8;
  EXPECT_EQ(tc_apply_fx_preset(f.e, &bad), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_apply_fx_preset(f.e, nullptr), TC_ERR_INVALID_ARG);
  tc_fx_preset wild = p;
  wild.delay_feedback = 3.0f;
  wild.comp_ratio = 0.1f;
  wild.eq_bands[0].gain_db = 99.0f;
  ASSERT_EQ(tc_apply_fx_preset(f.e, &wild), TC_OK);
  tc_fx_preset back{};
  back.struct_size = sizeof(back);
  ASSERT_EQ(tc_get_fx_preset(f.e, &back), TC_OK);
  EXPECT_EQ(back.delay_feedback, 0.5f);
  EXPECT_EQ(back.comp_ratio, 1.0f);
  EXPECT_EQ(back.eq_bands[0].gain_db, 18.0f);
}

// Tempo đọc / ghi (≤ 0 = 100 BPM); struct tc_metrics của binding v0.5 (không có trường FX) vẫn đọc
// được.
TEST(EngineFx, TempoAndOldMetricsStruct) {
  FxFixture f;
  ASSERT_EQ(tc_set_tempo(f.e, 92.0f), TC_OK);
  float bpm = 0.0f;
  ASSERT_EQ(tc_get_tempo(f.e, &bpm), TC_OK);
  EXPECT_EQ(bpm, 92.0f);
  ASSERT_EQ(tc_set_tempo(f.e, 0.0f), TC_OK);
  ASSERT_EQ(tc_get_tempo(f.e, &bpm), TC_OK);
  EXPECT_EQ(bpm, 100.0f);
  EXPECT_EQ(tc_set_tempo(f.e, NAN), TC_ERR_INVALID_ARG);
  tc_metrics old{};
  old.struct_size = offsetof(tc_metrics, algorithm_latency_frames) + sizeof(int32_t);
  EXPECT_EQ(tc_get_metrics(f.e, &old), TC_OK);
  EXPECT_EQ(old.algorithm_latency_frames, 48);
}
