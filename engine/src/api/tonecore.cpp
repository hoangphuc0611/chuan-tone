// Lớp C API: chuyển lời gọi C sang tonecore::Engine, kiểm tra tham số,
// và đảm bảo không exception nào lọt ra ngoài ranh giới C.

#include "tonecore/tonecore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/engine.h"
#include "fx/fx_presets.h"
#include "tonecore_version.h"

struct tc_engine {
  explicit tc_engine(const tonecore::EngineConfig& c) : impl(c) {}
  tonecore::Engine impl;
};

namespace {

constexpr uint32_t kMinSampleRate = 8000;
constexpr uint32_t kMaxSampleRate = 192000;
constexpr uint32_t kMaxBlockFrames = 8192;
constexpr uint32_t kMaxChannels = 8;

bool configIsValid(const tc_engine_config& c) {
  return c.sample_rate >= kMinSampleRate && c.sample_rate <= kMaxSampleRate &&
         c.max_block_frames >= 1 && c.max_block_frames <= kMaxBlockFrames &&
         c.input_channels >= 1 && c.input_channels <= kMaxChannels && c.output_channels >= 1 &&
         c.output_channels <= kMaxChannels &&
         (c.backend == TC_BACKEND_NULL || c.backend == TC_BACKEND_PLATFORM);
}

}  // namespace

namespace {

// ---- FX giọng (v0.6): tc_fx_preset ↔ FxPreset, kẹp mọi giá trị vào khoảng hợp lệ ----

float clampf(float v, float lo, float hi) {
  return std::min(hi, std::max(lo, v));
}

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

tonecore::SvfType bandType(int32_t t) {
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

tonecore::NoteDivision division(int32_t d) {
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

int32_t bandTypeC(tonecore::SvfType t) {
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

}  // namespace

extern "C" {

uint32_t tc_get_api_version(void) {
  return TC_API_VERSION;
}

const char* tc_get_version_string(void) {
  return TONECORE_VERSION_STRING;
}

const char* tc_result_to_string(tc_result result) {
  switch (result) {
    case TC_OK:
      return "TC_OK";
    case TC_ERR_INVALID_ARG:
      return "TC_ERR_INVALID_ARG";
    case TC_ERR_OUT_OF_MEMORY:
      return "TC_ERR_OUT_OF_MEMORY";
    case TC_ERR_INVALID_STATE:
      return "TC_ERR_INVALID_STATE";
    case TC_ERR_NOT_SUPPORTED:
      return "TC_ERR_NOT_SUPPORTED";
    case TC_ERR_INTERNAL:
      return "TC_ERR_INTERNAL";
    case TC_ERR_DEVICE:
      return "TC_ERR_DEVICE";
    case TC_ERR_CANCELLED:
      return "TC_ERR_CANCELLED";
  }
  return "TC_ERR_UNKNOWN";
}

void tc_engine_config_init(tc_engine_config* config) {
  if (config == nullptr) return;
  std::memset(config, 0, sizeof(*config));
  config->struct_size = sizeof(tc_engine_config);
  config->sample_rate = 48000;
  config->max_block_frames = 1024;
  config->input_channels = 1;
  config->output_channels = 2;
  config->backend = TC_BACKEND_NULL;
}

tc_result tc_engine_create(const tc_engine_config* config, tc_engine** out_engine) {
  if (out_engine == nullptr) return TC_ERR_INVALID_ARG;
  *out_engine = nullptr;

  tc_engine_config c;
  tc_engine_config_init(&c);
  if (config != nullptr) {
    // Chấp nhận struct cũ hơn (nhỏ hơn) — các trường thiếu giữ giá trị mặc định.
    if (config->struct_size < offsetof(tc_engine_config, backend) + sizeof(int32_t) ||
        config->struct_size > sizeof(tc_engine_config)) {
      return TC_ERR_INVALID_ARG;
    }
    std::memcpy(&c, config, config->struct_size);
  }
  if (!configIsValid(c)) return TC_ERR_INVALID_ARG;

  tonecore::EngineConfig ec;
  ec.sampleRate = c.sample_rate;
  ec.maxBlockFrames = c.max_block_frames;
  ec.inputChannels = c.input_channels;
  ec.outputChannels = c.output_channels;
  ec.backend = static_cast<tc_backend>(c.backend);

  try {
    *out_engine = new tc_engine(ec);
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_engine_start(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.start();
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_engine_stop(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.stop();
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

void tc_engine_destroy(tc_engine* engine) {
  if (engine == nullptr) return;
  try {
    if (engine->impl.state() == TC_STATE_RUNNING) engine->impl.stop();
    delete engine;
  } catch (...) {
    // Không để exception vượt ranh giới C; trường hợp này chỉ xảy ra khi hệ thống hỏng nặng.
  }
}

tc_result tc_set_param(tc_engine* engine, int32_t node_id, int32_t param_id, float value) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.setParam(node_id, param_id, value);
}

tc_result tc_get_param(tc_engine* engine, int32_t node_id, int32_t param_id, float* out_value) {
  if (engine == nullptr || out_value == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.getParam(node_id, param_id, out_value);
}

tc_result tc_engine_process_offline(tc_engine* engine, const float* input, float* output,
                                    uint32_t frames) {
  if (engine == nullptr || output == nullptr) return TC_ERR_INVALID_ARG;
  const auto& cfg = engine->impl.config();
  if (cfg.backend != TC_BACKEND_NULL) return TC_ERR_INVALID_STATE;
  if (frames > cfg.maxBlockFrames) return TC_ERR_INVALID_ARG;
  if (engine->impl.state() != TC_STATE_RUNNING) return TC_ERR_INVALID_STATE;
  engine->impl.process(input, output, frames);
  return TC_OK;
}

tc_result tc_get_metrics(tc_engine* engine, tc_metrics* out_metrics) {
  if (engine == nullptr || out_metrics == nullptr) return TC_ERR_INVALID_ARG;
  // Chấp nhận struct của binding cũ hơn (v0.1 kết thúc ở cpu_load): chỉ ghi phần người gọi có.
  constexpr uint32_t kMinSize = offsetof(tc_metrics, cpu_load) + sizeof(float);
  const uint32_t size = out_metrics->struct_size;
  if (size < kMinSize || size > sizeof(tc_metrics)) return TC_ERR_INVALID_ARG;
  tc_metrics full{};
  full.struct_size = size;
  engine->impl.fillMetrics(full);
  std::memcpy(out_metrics, &full, size);
  return TC_OK;
}

tc_result tc_load_beat(tc_engine* engine, const char* path_utf8) {
  if (engine == nullptr || path_utf8 == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.loadBeat(path_utf8);
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_load_beat_with_vocals(tc_engine* engine, const char* path_utf8,
                                   const char* const* vocal_paths_utf8, uint32_t vocal_count) {
  if (engine == nullptr || path_utf8 == nullptr || vocal_paths_utf8 == nullptr ||
      vocal_count == 0 || vocal_count > 4096) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    std::vector<std::string> vocals;
    vocals.reserve(vocal_count);
    for (uint32_t i = 0; i < vocal_count; ++i) {
      if (vocal_paths_utf8[i] == nullptr || vocal_paths_utf8[i][0] == '\0') {
        return TC_ERR_INVALID_ARG;
      }
      vocals.emplace_back(vocal_paths_utf8[i]);
    }
    return engine->impl.loadBeat(path_utf8, std::move(vocals));
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_play(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.play();
}

tc_result tc_pause(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.pause();
}

tc_result tc_seek(tc_engine* engine, double position_ms) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.seekMs(position_ms);
}

tc_result tc_get_playback_state(tc_engine* engine, tc_playback_state* out_state) {
  if (engine == nullptr || out_state == nullptr) return TC_ERR_INVALID_ARG;
  // Chấp nhận struct của binding v0.4 (kết thúc ở duration_ms): chỉ ghi phần người gọi có.
  constexpr uint32_t kMinSize = offsetof(tc_playback_state, duration_ms) + sizeof(double);
  const uint32_t size = out_state->struct_size;
  if (size < kMinSize || size > sizeof(tc_playback_state)) return TC_ERR_INVALID_ARG;
  tc_playback_state full{};
  full.struct_size = size;
  engine->impl.fillPlayback(full);
  std::memcpy(out_state, &full, size);
  return TC_OK;
}

tc_result tc_set_transpose(tc_engine* engine, int32_t semitones) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.setTranspose(semitones);
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_get_transpose(tc_engine* engine, int32_t* out_semitones) {
  if (engine == nullptr || out_semitones == nullptr) return TC_ERR_INVALID_ARG;
  *out_semitones = engine->impl.transpose();
  return TC_OK;
}

void tc_fx_preset_init(tc_fx_preset* preset, int32_t genre) {
  if (preset == nullptr) return;
  const auto g = genre >= TC_GENRE_DRY && genre <= TC_GENRE_LOFI
                     ? static_cast<tonecore::Genre>(genre)
                     : tonecore::Genre::Dry;
  fromFx(tonecore::genrePreset(g), *preset);
}

tc_result tc_apply_fx_preset(tc_engine* engine, const tc_fx_preset* preset) {
  if (engine == nullptr || preset == nullptr || preset->struct_size != sizeof(tc_fx_preset) ||
      !allFinite(*preset)) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    engine->impl.applyFxPreset(toFx(*preset));
    return TC_OK;
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_get_fx_preset(tc_engine* engine, tc_fx_preset* out_preset) {
  if (engine == nullptr || out_preset == nullptr ||
      out_preset->struct_size != sizeof(tc_fx_preset)) {
    return TC_ERR_INVALID_ARG;
  }
  fromFx(engine->impl.fxPreset(), *out_preset);
  return TC_OK;
}

tc_result tc_set_tempo(tc_engine* engine, float bpm) {
  if (engine == nullptr || !std::isfinite(bpm)) return TC_ERR_INVALID_ARG;
  try {
    engine->impl.setTempo(bpm);
    return TC_OK;
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_get_tempo(tc_engine* engine, float* out_bpm) {
  if (engine == nullptr || out_bpm == nullptr) return TC_ERR_INVALID_ARG;
  *out_bpm = engine->impl.tempo();
  return TC_OK;
}

tc_result tc_set_key(tc_engine* engine, int32_t root, int32_t scale) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.setKey(root, scale);
}

tc_result tc_get_key(tc_engine* engine, int32_t* out_root, int32_t* out_scale) {
  if (engine == nullptr || out_root == nullptr || out_scale == nullptr) return TC_ERR_INVALID_ARG;
  engine->impl.key(*out_root, *out_scale);
  return TC_OK;
}

tc_result tc_get_applied_key(tc_engine* engine, int32_t* out_root, int32_t* out_scale) {
  if (engine == nullptr || out_root == nullptr || out_scale == nullptr) return TC_ERR_INVALID_ARG;
  engine->impl.appliedKey(*out_root, *out_scale);
  return TC_OK;
}

tc_result tc_set_correction_preset(tc_engine* engine, int32_t preset) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.setCorrectionPreset(preset);
}

tc_result tc_set_tuning(tc_engine* engine, float cents) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.setTuning(cents);
}

tc_result tc_get_tuning(tc_engine* engine, float* out_cents) {
  if (engine == nullptr || out_cents == nullptr) return TC_ERR_INVALID_ARG;
  *out_cents = engine->impl.tuning();
  return TC_OK;
}

tc_result tc_set_key_timeline(tc_engine* engine, const tc_key_segment* segments, uint32_t count) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.setKeyTimeline(segments, count);
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_get_pitch_state(tc_engine* engine, tc_pitch_state* out_state) {
  if (engine == nullptr || out_state == nullptr) return TC_ERR_INVALID_ARG;
  if (out_state->struct_size != sizeof(tc_pitch_state)) return TC_ERR_INVALID_ARG;
  engine->impl.fillPitchState(*out_state);
  return TC_OK;
}

tc_result tc_set_route_hint(tc_engine* engine, int32_t route) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.setRouteHint(route);
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_measure_latency_start(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  try {
    return engine->impl.startLatencyMeasurement();
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_get_latency_result(tc_engine* engine, tc_latency_result* out_result) {
  if (engine == nullptr || out_result == nullptr) return TC_ERR_INVALID_ARG;
  if (out_result->struct_size == 0 || out_result->struct_size > sizeof(tc_latency_result)) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    return engine->impl.latencyResult(*out_result);
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

int32_t tc_poll_events(tc_engine* engine, tc_event* out_events, uint32_t max_events) {
  if (engine == nullptr || (out_events == nullptr && max_events > 0)) return TC_ERR_INVALID_ARG;
  try {
    return static_cast<int32_t>(engine->impl.pollEvents(out_events, max_events));
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

}  // extern "C"
