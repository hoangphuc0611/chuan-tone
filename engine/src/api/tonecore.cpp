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
  if (c.backend == TC_BACKEND_PLATFORM) return TC_ERR_NOT_SUPPORTED;  // có từ M1

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
  return engine->impl.start();
}

tc_result tc_engine_stop(tc_engine* engine) {
  if (engine == nullptr) return TC_ERR_INVALID_ARG;
  return engine->impl.stop();
}

void tc_engine_destroy(tc_engine* engine) {
  if (engine == nullptr) return;
  if (engine->impl.state() == TC_STATE_RUNNING) engine->impl.stop();
  delete engine;
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
  if (out_metrics->struct_size != sizeof(tc_metrics)) return TC_ERR_INVALID_ARG;
  engine->impl.fillMetrics(*out_metrics);
  return TC_OK;
}

}  // extern "C"
