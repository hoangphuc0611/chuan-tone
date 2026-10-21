// Lớp C API: chuyển lời gọi C sang tonecore::Engine, kiểm tra tham số,
// và đảm bảo không exception nào lọt ra ngoài ranh giới C.

#include "tonecore/tonecore.h"

#include <cstddef>
#include <cstring>
#include <new>

#include "core/engine.h"
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
  if (out_state->struct_size != sizeof(tc_playback_state)) return TC_ERR_INVALID_ARG;
  engine->impl.fillPlayback(*out_state);
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

tc_result tc_set_correction_preset(tc_engine* engine, int32_t preset) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.setCorrectionPreset(preset);
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
