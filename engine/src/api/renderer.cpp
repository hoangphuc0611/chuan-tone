// Lớp C API render lại bản thu (tc_renderer_*, từ v0.8, docs/m7-plan.md B3).
//
// Như tc_exporter: mỗi renderer một Worker (luồng nền); tiến độ / số đo là atomic trong
// TakeRenderProgress, UI kéo bằng tc_renderer_get_status.

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#include "api/fx_convert.h"
#include "core/worker.h"
#include "record/take_renderer.h"
#include "tonecore/tonecore.h"

struct tc_renderer {
  std::mutex mutex;
  std::shared_ptr<tonecore::TakeRenderProgress> progress;  // bảo vệ bởi mutex
  int32_t state = TC_ANALYSIS_IDLE;                        // bảo vệ bởi mutex
  tc_result result = TC_OK;                                // bảo vệ bởi mutex
  // Khai báo cuối để bị huỷ đầu tiên: join luồng nền trước khi các thành viên khác mất.
  tonecore::Worker worker;
};

namespace {

constexpr int32_t kScaleCount = TC_SCALE_CHROMATIC + 1;

bool finite(float v) {
  return std::isfinite(v);
}

// Kiểm tra + chuyển cấu hình C sang thông số render. false = TC_ERR_INVALID_ARG.
bool toSettings(const tc_render_config& c, tonecore::TakeRenderSettings& s) {
  using namespace tonecore;
  if (c.struct_size != sizeof(tc_render_config) || !capi::allFinite(c.fx)) return false;
  if (!finite(c.fx_wet) || !finite(c.tempo_bpm) || !finite(c.tuning_cents) ||
      !finite(c.gate_threshold_db) || !finite(c.voice_gain) || !finite(c.beat_gain) ||
      !finite(c.target_lufs) || !finite(c.true_peak_db)) {
    return false;
  }
  if (c.key_root < 0 || c.key_root > 11 || c.key_scale < 0 || c.key_scale >= kScaleCount ||
      c.correction_preset < TC_PRESET_NATURAL || c.correction_preset > TC_PRESET_ROBOT ||
      c.format < TC_RENDER_WAV16 || c.format > TC_RENDER_M4A || c.voice_gain < 0.0f ||
      c.beat_gain < 0.0f || (c.end_frame != 0 && c.end_frame <= c.start_frame) ||
      (c.key_segment_count > 0 && c.key_segments == nullptr) || c.key_segment_count > 256) {
    return false;
  }
  s.voiceOffsetFrames = c.voice_offset_frames;
  s.startFrame = c.start_frame;
  s.endFrame = c.end_frame;
  s.fx = capi::toFx(c.fx);
  s.fxEnabled = c.fx_enabled != 0;
  s.fxWet = std::clamp(c.fx_wet, 0.0f, 1.0f);
  s.tempoBpm = c.tempo_bpm > 0.0f ? std::clamp(c.tempo_bpm, 30.0f, 300.0f) : 100.0f;
  s.correctionEnabled = c.correction_enabled != 0;
  s.correction = static_cast<CorrectionPreset>(c.correction_preset);
  s.keyRoot = c.key_root;
  s.keyScale = static_cast<Scale>(c.key_scale);
  s.tuningCents = std::clamp(c.tuning_cents, -100.0f, 100.0f);
  s.gateEnabled = c.gate_enabled != 0;
  s.gateThresholdDb = std::clamp(c.gate_threshold_db, -80.0f, 0.0f);
  s.voiceGain = std::min(c.voice_gain, 4.0f);
  s.beatGain = std::min(c.beat_gain, 4.0f);
  s.normalize = c.normalize != 0;
  s.targetLufs = std::clamp(c.target_lufs, -40.0f, -5.0f);
  s.truePeakDb = std::clamp(c.true_peak_db, -12.0f, 0.0f);
  s.format = c.format;
  s.aacBitrate = c.aac_bitrate > 0 ? std::clamp(c.aac_bitrate, 64000, 320000) : 192000;
  s.timeline.clear();
  double lastMs = -1.0;
  for (uint32_t i = 0; i < c.key_segment_count; ++i) {
    const tc_key_segment& g = c.key_segments[i];
    if (!std::isfinite(g.start_ms) || g.start_ms < 0.0 || g.start_ms < lastMs || g.root < 0 ||
        g.root > 11 || g.scale < 0 || g.scale >= kScaleCount) {
      return false;
    }
    lastMs = g.start_ms;
    s.timeline.push_back({g.start_ms, g.root, static_cast<Scale>(g.scale)});
  }
  return true;
}

void runJob(tc_renderer* rd, const std::shared_ptr<tonecore::TakeRenderProgress>& prog,
            tonecore::TakeRenderSettings s) {
  tc_result r = TC_ERR_INTERNAL;
  try {
    r = tonecore::renderTake(s, *prog);
  } catch (const std::bad_alloc&) {
    r = TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    r = TC_ERR_INTERNAL;
  }
  std::lock_guard<std::mutex> lock(rd->mutex);
  rd->result = r;
  rd->state = r == TC_OK              ? TC_ANALYSIS_DONE
              : r == TC_ERR_CANCELLED ? TC_ANALYSIS_CANCELLED
                                      : TC_ANALYSIS_FAILED;
}

}  // namespace

extern "C" {

void tc_render_config_init(tc_render_config* config) {
  if (config == nullptr) return;
  *config = tc_render_config{};
  config->struct_size = sizeof(tc_render_config);
  tc_fx_preset_init(&config->fx, TC_GENRE_DRY);
  config->fx_enabled = 1;
  config->fx_wet = 1.0f;
  config->tempo_bpm = 100.0f;
  config->correction_preset = TC_PRESET_NATURAL;
  config->key_root = 9;
  config->key_scale = TC_SCALE_NATURAL_MINOR;
  config->gate_threshold_db = -50.0f;
  config->voice_gain = 1.0f;
  config->beat_gain = 1.0f;
  config->normalize = 1;
  config->target_lufs = -14.0f;
  config->true_peak_db = -1.0f;
  config->format = TC_RENDER_WAV16;
  config->aac_bitrate = 192000;
}

tc_result tc_renderer_create(tc_renderer** out_renderer) {
  if (out_renderer == nullptr) return TC_ERR_INVALID_ARG;
  *out_renderer = nullptr;
  try {
    *out_renderer = new tc_renderer();
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_renderer_start(tc_renderer* renderer, const char* beat_path_utf8,
                            const char* voice_path_utf8, const char* out_path_utf8,
                            const tc_render_config* config) {
  if (renderer == nullptr || beat_path_utf8 == nullptr || beat_path_utf8[0] == '\0' ||
      voice_path_utf8 == nullptr || voice_path_utf8[0] == '\0' || out_path_utf8 == nullptr ||
      out_path_utf8[0] == '\0' || config == nullptr) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    tonecore::TakeRenderSettings s;
    if (!toSettings(*config, s)) return TC_ERR_INVALID_ARG;
    s.beatPath = beat_path_utf8;
    s.voicePath = voice_path_utf8;
    s.outPath = out_path_utf8;
    std::lock_guard<std::mutex> lock(renderer->mutex);
    if (renderer->state == TC_ANALYSIS_RUNNING) return TC_ERR_INVALID_STATE;
    auto prog = std::make_shared<tonecore::TakeRenderProgress>();
    renderer->progress = prog;
    renderer->state = TC_ANALYSIS_RUNNING;
    renderer->result = TC_OK;
    renderer->worker.post(
        [renderer, prog, s = std::move(s)]() mutable { runJob(renderer, prog, std::move(s)); });
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_renderer_get_status(tc_renderer* renderer, tc_render_status* out_status) {
  if (renderer == nullptr || out_status == nullptr) return TC_ERR_INVALID_ARG;
  if (out_status->struct_size != sizeof(tc_render_status)) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(renderer->mutex);
  tc_render_status s{};
  s.struct_size = sizeof(tc_render_status);
  s.state = renderer->state;
  s.result = renderer->result;
  if (const auto& p = renderer->progress) {
    s.progress = p->progress.load();
    s.sample_rate = p->sampleRate.load();
    s.frames = p->frames.load();
    s.mix_lufs = p->mixLufs.load();
    s.output_lufs = p->outputLufs.load();
    s.true_peak_db = p->truePeakDb.load();
    s.gain_db = p->gainDb.load();
  }
  *out_status = s;
  return TC_OK;
}

tc_result tc_renderer_cancel(tc_renderer* renderer) {
  if (renderer == nullptr) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(renderer->mutex);
  if (renderer->progress) renderer->progress->cancelled.store(true);
  return TC_OK;
}

void tc_renderer_destroy(tc_renderer* renderer) {
  if (renderer == nullptr) return;
  try {
    tc_renderer_cancel(renderer);
    delete renderer;  // Worker join luồng nền (dừng ở lần kiểm tra cờ huỷ kế tiếp)
  } catch (...) {
    // Không để exception vượt ranh giới C.
  }
}

}  // extern "C"
