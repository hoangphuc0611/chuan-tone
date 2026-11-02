// tc_fx_preset ↔ FxPreset (C API v0.6), dùng chung cho engine và bộ render bản thu (v0.8).

#include "api/fx_convert.h"

#include <algorithm>
#include <cmath>

namespace tonecore::capi {

namespace {

// ---- FX giọng (v0.6): tc_fx_preset ↔ FxPreset, kẹp mọi giá trị vào khoảng hợp lệ ----

float clampf(float v, float lo, float hi) {
  return std::min(hi, std::max(lo, v));
}

}  // namespace

bool allFinite(const tc_fx_preset& c) {
  const float v[] = {
      c.eq_high_pass_hz,     c.eq_low_pass_hz,         c.comp_threshold_db,  c.comp_ratio,
      c.comp_knee_db,        c.comp_attack_ms,         c.comp_release_ms,    c.comp_makeup_db,
      c.deess_threshold_db,  c.deess_max_reduction_db, c.delay_ms,           c.delay_feedback,
      c.delay_mix,           c.delay_low_pass_hz,      c.delay_high_pass_hz, c.reverb_decay_s,
      c.reverb_pre_delay_ms, c.reverb_damping,         c.reverb_mix};
  for (const float x : v) {
    if (!std::isfinite(x)) return false;
  }
  for (const auto& b : c.eq_bands) {
    if (!std::isfinite(b.freq_hz) || !std::isfinite(b.gain_db) || !std::isfinite(b.q)) return false;
  }
  return true;
}

static tonecore::SvfType bandType(int32_t t) {
  switch (t) {
    case TC_EQ_BELL:
      return tonecore::SvfType::Bell;
    case TC_EQ_LOW_SHELF:
      return tonecore::SvfType::LowShelf;
    case TC_EQ_HIGH_SHELF:
      return tonecore::SvfType::HighShelf;
    default:
      return tonecore::SvfType::Off;
  }
}

static tonecore::NoteDivision division(int32_t d) {
  switch (d) {
    case TC_DELAY_QUARTER:
      return tonecore::NoteDivision::Quarter;
    case TC_DELAY_EIGHTH:
      return tonecore::NoteDivision::Eighth;
    case TC_DELAY_DOTTED_EIGHTH:
      return tonecore::NoteDivision::DottedEighth;
    case TC_DELAY_SIXTEENTH:
      return tonecore::NoteDivision::Sixteenth;
    case TC_DELAY_MS:
      return tonecore::NoteDivision::Milliseconds;
    default:
      return tonecore::NoteDivision::Off;
  }
}

tonecore::FxPreset toFx(const tc_fx_preset& c) {
  using namespace tonecore;
  FxPreset p;
  p.eq.highPassHz = c.eq_high_pass_hz > 0.0f ? clampf(c.eq_high_pass_hz, 20.0f, 2000.0f) : 0.0f;
  p.eq.lowPassHz = c.eq_low_pass_hz > 0.0f ? clampf(c.eq_low_pass_hz, 1000.0f, 20000.0f) : 0.0f;
  for (int i = 0; i < EqParams::kBands; ++i) {
    const tc_eq_band& b = c.eq_bands[i];
    p.eq.bands[i] = {bandType(b.type), clampf(b.freq_hz, 20.0f, 20000.0f),
                     clampf(b.gain_db, -18.0f, 18.0f), clampf(b.q, 0.1f, 10.0f)};
  }
  p.compOn = c.comp_enabled != 0;
  p.comp.thresholdDb = clampf(c.comp_threshold_db, -60.0f, 0.0f);
  p.comp.ratio = clampf(c.comp_ratio, 1.0f, 20.0f);
  p.comp.kneeDb = clampf(c.comp_knee_db, 0.0f, 24.0f);
  p.comp.attackMs = clampf(c.comp_attack_ms, 0.1f, 200.0f);
  p.comp.releaseMs = clampf(c.comp_release_ms, 5.0f, 2000.0f);
  p.comp.makeupDb = clampf(c.comp_makeup_db, -12.0f, 24.0f);
  p.deessOn = c.deess_enabled != 0;
  p.deess.thresholdDb = clampf(c.deess_threshold_db, -80.0f, 0.0f);
  p.deess.maxReductionDb = clampf(c.deess_max_reduction_db, 0.0f, 12.0f);
  p.delay.division = division(c.delay_division);
  p.delay.timeMs = clampf(c.delay_ms, 1.0f, 2000.0f);
  p.delay.feedback = clampf(c.delay_feedback, 0.0f, Delay::kMaxFeedback);
  p.delay.mix = clampf(c.delay_mix, 0.0f, 1.0f);
  p.delay.lowPassHz =
      c.delay_low_pass_hz > 0.0f ? clampf(c.delay_low_pass_hz, 500.0f, 20000.0f) : 0.0f;
  p.delay.highPassHz =
      c.delay_high_pass_hz > 0.0f ? clampf(c.delay_high_pass_hz, 20.0f, 2000.0f) : 0.0f;
  p.reverbOn = c.reverb_enabled != 0;
  p.reverb.type = c.reverb_type == TC_REVERB_ROOM    ? ReverbType::Room
                  : c.reverb_type == TC_REVERB_PLATE ? ReverbType::Plate
                                                     : ReverbType::Hall;
  p.reverb.decaySec = clampf(c.reverb_decay_s, 0.2f, 8.0f);
  p.reverb.preDelayMs = clampf(c.reverb_pre_delay_ms, 0.0f, 100.0f);
  p.reverb.damping = clampf(c.reverb_damping, 0.0f, 0.9f);
  p.reverb.mix = clampf(c.reverb_mix, 0.0f, 1.0f);
  return p;
}

static int32_t bandTypeC(tonecore::SvfType t) {
  switch (t) {
    case tonecore::SvfType::Bell:
      return TC_EQ_BELL;
    case tonecore::SvfType::LowShelf:
      return TC_EQ_LOW_SHELF;
    case tonecore::SvfType::HighShelf:
      return TC_EQ_HIGH_SHELF;
    default:
      return TC_EQ_OFF;
  }
}

void fromFx(const tonecore::FxPreset& p, tc_fx_preset& c) {
  using namespace tonecore;
  c = tc_fx_preset{};
  c.struct_size = sizeof(tc_fx_preset);
  c.eq_high_pass_hz = p.eq.highPassHz;
  c.eq_low_pass_hz = p.eq.lowPassHz;
  for (int i = 0; i < EqParams::kBands; ++i) {
    const EqBand& b = p.eq.bands[i];
    c.eq_bands[i] = {bandTypeC(b.type), b.freqHz, b.gainDb, b.q};
  }
  c.comp_enabled = p.compOn ? 1 : 0;
  c.comp_threshold_db = p.comp.thresholdDb;
  c.comp_ratio = p.comp.ratio;
  c.comp_knee_db = p.comp.kneeDb;
  c.comp_attack_ms = p.comp.attackMs;
  c.comp_release_ms = p.comp.releaseMs;
  c.comp_makeup_db = p.comp.makeupDb;
  c.deess_enabled = p.deessOn ? 1 : 0;
  c.deess_threshold_db = p.deess.thresholdDb;
  c.deess_max_reduction_db = p.deess.maxReductionDb;
  c.delay_division = static_cast<int32_t>(p.delay.division);  // cùng giá trị với tc_delay_division
  c.delay_ms = p.delay.timeMs;
  c.delay_feedback = p.delay.feedback;
  c.delay_mix = p.delay.mix;
  c.delay_low_pass_hz = p.delay.lowPassHz;
  c.delay_high_pass_hz = p.delay.highPassHz;
  c.reverb_enabled = p.reverbOn ? 1 : 0;
  c.reverb_type = static_cast<int32_t>(p.reverb.type);  // cùng giá trị với tc_reverb_type
  c.reverb_decay_s = p.reverb.decaySec;
  c.reverb_pre_delay_ms = p.reverb.preDelayMs;
  c.reverb_damping = p.reverb.damping;
  c.reverb_mix = p.reverb.mix;
}

}  // namespace tonecore::capi
