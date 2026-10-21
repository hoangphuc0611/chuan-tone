// Test đầu-cuối khối "Tự chỉnh giọng" (VoicePitchNode) và C API v0.3.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "pitch/pitch_analyzer.h"
#include "pitch/voice_pitch_node.h"
#include "tonecore/tonecore.h"
#include "util/synth_voice.h"

using namespace tonecore;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> runNode(VoicePitchNode& node, std::vector<float> x, uint32_t block = 128) {
  for (std::size_t off = 0; off < x.size(); off += block) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(block, x.size() - off));
    node.process(x.data() + off, n);
  }
  return x;
}

double medianF0(const std::vector<float>& x, double from, double to) {
  PitchAnalyzer pa(PitchAnalyzerConfig{});
  std::vector<double> v;
  pa.process(x.data(), x.size(), [&](const PitchFrame& f) {
    if (f.timeSec >= from && f.timeSec < to && f.tracked.voiced) v.push_back(f.tracked.f0);
  });
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

double cents(double a, double b) {
  return 1200.0 * std::log2(a / b);
}

}  // namespace

TEST(VoicePitchNode, RobotPullsSharpNoteToTarget) {
  VoicePitchNode node(kFs, 1024);
  node.setKey(9, Scale::NaturalMinor);  // La thứ
  node.applyPreset(CorrectionPreset::Robot);
  node.setEnabled(true);
  // Hát La4 cao 30 cent.
  const auto s = test::synthVoice(test::offset(440.0, 30.0), 1.5);
  const auto y = runNode(node, s.audio);
  EXPECT_LT(std::fabs(cents(medianF0(s.audio, 0.3, 1.4), 440.0) - 30.0), 3.0);  // đầu vào đúng +30
  EXPECT_LT(std::fabs(cents(medianF0(y, 0.3, 1.4), 440.0)), 5.0);               // đầu ra về La
}

TEST(VoicePitchNode, AllPresetsCorrectSteadyNote) {
  for (auto preset :
       {CorrectionPreset::Natural, CorrectionPreset::Singer, CorrectionPreset::Robot}) {
    VoicePitchNode node(kFs, 1024);
    node.setKey(0, Scale::Major);
    node.applyPreset(preset);
    node.setEnabled(true);
    // Sol4 thấp 35 cent (vượt flex 10 cent của Tự nhiên).
    const auto s = test::synthVoice(test::offset(test::midiToHz(67), -35.0), 2.0);
    const auto y = runNode(node, s.audio, 256);
    EXPECT_LT(std::fabs(cents(medianF0(y, 0.8, 1.9), test::midiToHz(67))), 8.0)
        << static_cast<int>(preset);
  }
}

TEST(VoicePitchNode, DisabledIsZeroLatencyHighpassOnly) {
  VoicePitchNode node(kFs, 1024);
  EXPECT_EQ(node.algorithmLatency(), 0);
  const auto s = test::synthVoice(test::steady(220.0), 0.5);
  const auto y = runNode(node, s.audio);
  Biquad ref;
  ref.setCoeffs(BiquadCoeffs::highpass(kFs, 80.0));
  std::vector<float> expect(s.audio.size());
  ref.process(s.audio.data(), expect.data(), expect.size());
  for (std::size_t i = 0; i < y.size(); ++i) ASSERT_FLOAT_EQ(y[i], expect[i]) << i;
}

TEST(VoicePitchNode, ToggleHasNoClicksAndReportsLatency) {
  VoicePitchNode node(kFs, 1024);
  node.setKey(0, Scale::Chromatic);
  node.applyPreset(CorrectionPreset::Singer);
  const auto s = test::synthVoice(test::steady(196.0), 2.0);
  std::vector<float> x = s.audio;
  float maxStepIn = 0.0f;
  for (std::size_t i = 1; i < x.size(); ++i) {
    maxStepIn = std::max(maxStepIn, std::fabs(x[i] - x[i - 1]));
  }
  for (std::size_t off = 0; off < x.size(); off += 128) {
    // Bật ở 0,5 s, tắt ở 1,0 s, bật lại ở 1,5 s.
    const double t = static_cast<double>(off) / kFs;
    node.setEnabled((t >= 0.5 && t < 1.0) || t >= 1.5);
    node.process(x.data() + off, 128);
  }
  EXPECT_EQ(node.algorithmLatency(), 480);
  float maxStepOut = 0.0f;
  for (std::size_t i = 4800; i < x.size(); ++i) {
    maxStepOut = std::max(maxStepOut, std::fabs(x[i] - x[i - 1]));
  }
  EXPECT_LT(maxStepOut, 2.0f * maxStepIn);
}

TEST(VoicePitchNode, PublishesPitchState) {
  VoicePitchNode node(kFs, 1024);
  node.setKey(9, Scale::NaturalMinor);
  const auto s = test::synthVoice(test::offset(440.0, 30.0), 0.5);
  runNode(node, s.audio);
  PitchSnapshot snap;
  ASSERT_TRUE(node.readState(snap));
  EXPECT_TRUE(snap.voiced);
  EXPECT_NEAR(snap.f0, 440.0 * std::pow(2.0, 30.0 / 1200.0), 2.0);
  EXPECT_EQ(snap.targetNote, 69);
  EXPECT_NEAR(snap.centsOff, 30.0f, 5.0f);
}

// ---------------- C API v0.3 ----------------

TEST(CApiPitch, KeyPresetParamsAndState) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);

  EXPECT_EQ(tc_set_key(e, 12, TC_SCALE_MAJOR), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_key(e, 0, 6), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_key(nullptr, 0, 0), TC_ERR_INVALID_ARG);
  ASSERT_EQ(tc_set_key(e, 9, TC_SCALE_NATURAL_MINOR), TC_OK);
  int32_t root = -1, scale = -1;
  ASSERT_EQ(tc_get_key(e, &root, &scale), TC_OK);
  EXPECT_EQ(root, 9);
  EXPECT_EQ(scale, TC_SCALE_NATURAL_MINOR);

  EXPECT_EQ(tc_set_correction_preset(e, 3), TC_ERR_INVALID_ARG);
  ASSERT_EQ(tc_set_correction_preset(e, TC_PRESET_NATURAL), TC_OK);
  float v = 0.0f;
  ASSERT_EQ(tc_get_param(e, TC_NODE_PITCH, TC_PARAM_RETUNE_MS, &v), TC_OK);
  EXPECT_EQ(v, 40.0f);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_RETUNE_MS, 500.0f), TC_OK);  // bị kẹp
  ASSERT_EQ(tc_get_param(e, TC_NODE_PITCH, TC_PARAM_RETUNE_MS, &v), TC_OK);
  EXPECT_EQ(v, 200.0f);
  EXPECT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_HUMANIZE, NAN), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_GAIN, 1.0f), TC_ERR_INVALID_ARG);

  tc_metrics m{};
  m.struct_size = sizeof(m);
  ASSERT_EQ(tc_get_metrics(e, &m), TC_OK);
  EXPECT_EQ(m.algorithm_latency_frames, 0);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_get_metrics(e, &m), TC_OK);
  EXPECT_EQ(m.algorithm_latency_frames, 480);

  // Cho giọng tổng hợp qua engine rồi đọc trạng thái cao độ.
  const auto s = test::synthVoice(test::offset(440.0, 30.0), 0.5);
  std::vector<float> out(2 * 256);
  for (std::size_t off = 0; off + 256 <= s.audio.size(); off += 256) {
    ASSERT_EQ(tc_engine_process_offline(e, s.audio.data() + off, out.data(), 256), TC_OK);
  }
  tc_pitch_state ps{};
  EXPECT_EQ(tc_get_pitch_state(e, &ps), TC_ERR_INVALID_ARG);  // thiếu struct_size
  ps.struct_size = sizeof(ps);
  ASSERT_EQ(tc_get_pitch_state(e, &ps), TC_OK);
  EXPECT_EQ(ps.voiced, 1);
  EXPECT_EQ(ps.enabled, 1);
  EXPECT_EQ(ps.target_note, 69);
  EXPECT_NEAR(ps.cents_off, 30.0f, 5.0f);
  EXPECT_LT(ps.correction_cents, -10.0f);  // đang kéo xuống

  // Binding v0.2 (struct metrics ngắn hơn) vẫn dùng được.
  tc_metrics old{};
  old.struct_size = offsetof(tc_metrics, cpu_load_peak);
  EXPECT_EQ(tc_get_metrics(e, &old), TC_OK);

  tc_engine_destroy(e);
}
