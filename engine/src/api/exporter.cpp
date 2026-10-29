// Lớp C API xuất FLAC (tc_exporter_*, từ v0.7) — gửi bài lên server tách giọng (docs/m6-plan.md).
//
// Như tc_analyzer: mỗi exporter một Worker (luồng nền); tiến độ / số đoạn đã ghi là atomic trong
// ExportControl, UI kéo bằng tc_exporter_get_status. Mỗi đoạn ghi qua tên tạm rồi đổi tên → luồng
// upload thấy file nào là file đó đã đủ.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#include "codec/flac_encoder.h"
#include "core/worker.h"
#include "decode/decoder.h"
#include "dsp/resampler.h"
#include "tonecore/tonecore.h"

namespace {

struct ExportControl {
  std::atomic<bool> cancelled{false};
  std::atomic<float> progress{0.0f};
  std::atomic<uint32_t> chunksDone{0};
  std::atomic<uint32_t> chunksTotal{0};
  std::atomic<uint32_t> sampleRate{0};
  std::atomic<uint32_t> channels{0};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> chunkFrames{0};
};

constexpr tc_result kCancelled = TC_ERR_CANCELLED;

std::string chunkPath(const std::string& prefix, uint32_t index, bool single) {
  if (single) return prefix + ".flac";
  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "_%03u.flac", index);
  return prefix + suffix;
}

tc_result runExport(ExportControl& ctl, const std::string& in, const std::string& prefix,
                    const tc_export_config& cfg) {
  tonecore::DecodedAudio audio;
  tc_result r = tonecore::decodeFile(in, audio);
  if (r != TC_OK) return r;
  if (audio.frames == 0 || audio.channels == 0) return TC_ERR_NOT_SUPPORTED;
  const double seconds = static_cast<double>(audio.frames) / audio.sampleRate;
  if (cfg.max_seconds > 0.0f && seconds > static_cast<double>(cfg.max_seconds)) {
    return TC_ERR_NOT_SUPPORTED;
  }
  if (ctl.cancelled.load()) return kCancelled;
  ctl.progress.store(0.3f);

  uint32_t rate = audio.sampleRate;
  if (cfg.sample_rate != 0 && cfg.sample_rate != rate) {
    const tonecore::Resampler rs(rate, cfg.sample_rate);
    audio.samples = rs.process(audio.samples.data(), static_cast<std::size_t>(audio.frames),
                               static_cast<int>(audio.channels));
    rate = cfg.sample_rate;
    audio.frames = audio.samples.size() / audio.channels;
    if (ctl.cancelled.load()) return kCancelled;
  }
  ctl.progress.store(0.4f);

  const bool single = !(cfg.chunk_seconds > 0.0f);
  const uint64_t chunkFrames =
      single ? audio.frames
             : std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(
                                         static_cast<double>(cfg.chunk_seconds) * rate)));
  const auto total = static_cast<uint32_t>((audio.frames + chunkFrames - 1) / chunkFrames);
  ctl.sampleRate.store(rate);
  ctl.channels.store(audio.channels);
  ctl.frames.store(audio.frames);
  ctl.chunkFrames.store(chunkFrames);
  ctl.chunksTotal.store(total);

  for (uint32_t i = 0; i < total; ++i) {
    if (ctl.cancelled.load()) return kCancelled;
    const uint64_t start = i * chunkFrames;
    const uint64_t n = std::min<uint64_t>(chunkFrames, audio.frames - start);
    const auto data = tonecore::encodeFlac(audio.samples.data() + start * audio.channels, n,
                                           audio.channels, rate);
    r = tonecore::writeFileAtomic(chunkPath(prefix, i, single), data);
    if (r != TC_OK) return r;
    ctl.chunksDone.store(i + 1);
    ctl.progress.store(0.4f + 0.6f * static_cast<float>(i + 1) / static_cast<float>(total));
  }
  return TC_OK;
}

}  // namespace

struct tc_exporter {
  std::mutex mutex;
  std::shared_ptr<ExportControl> control;  // bảo vệ bởi mutex
  int32_t state = TC_ANALYSIS_IDLE;        // bảo vệ bởi mutex
  tc_result result = TC_OK;                // bảo vệ bởi mutex
  // Khai báo cuối để bị huỷ đầu tiên: join luồng nền trước khi các thành viên khác mất.
  tonecore::Worker worker;
};

namespace {

void runJob(tc_exporter* e, const std::shared_ptr<ExportControl>& ctl, const std::string& in,
            const std::string& prefix, const tc_export_config cfg) {
  tc_result r = TC_ERR_INTERNAL;
  try {
    r = runExport(*ctl, in, prefix, cfg);
  } catch (const std::bad_alloc&) {
    r = TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    r = TC_ERR_INTERNAL;
  }
  std::lock_guard<std::mutex> lock(e->mutex);
  e->result = r;
  e->state = r == TC_OK        ? TC_ANALYSIS_DONE
             : r == kCancelled ? TC_ANALYSIS_CANCELLED
                               : TC_ANALYSIS_FAILED;
  if (r == TC_OK) ctl->progress.store(1.0f);
}

}  // namespace

extern "C" {

void tc_export_config_init(tc_export_config* config) {
  if (config == nullptr) return;
  *config = tc_export_config{};
  config->struct_size = sizeof(tc_export_config);
}

tc_result tc_exporter_create(tc_exporter** out_exporter) {
  if (out_exporter == nullptr) return TC_ERR_INVALID_ARG;
  *out_exporter = nullptr;
  try {
    *out_exporter = new tc_exporter();
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_exporter_start(tc_exporter* exporter, const char* in_path_utf8,
                            const char* out_prefix_utf8, const tc_export_config* config) {
  if (exporter == nullptr || in_path_utf8 == nullptr || in_path_utf8[0] == '\0' ||
      out_prefix_utf8 == nullptr || out_prefix_utf8[0] == '\0' || config == nullptr ||
      config->struct_size != sizeof(tc_export_config) || !std::isfinite(config->chunk_seconds) ||
      !std::isfinite(config->max_seconds) || config->chunk_seconds < 0.0f ||
      config->sample_rate > 384000) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    std::lock_guard<std::mutex> lock(exporter->mutex);
    if (exporter->state == TC_ANALYSIS_RUNNING) return TC_ERR_INVALID_STATE;
    auto ctl = std::make_shared<ExportControl>();
    exporter->control = ctl;
    exporter->state = TC_ANALYSIS_RUNNING;
    exporter->result = TC_OK;
    exporter->worker.post([exporter, ctl, in = std::string(in_path_utf8),
                           prefix = std::string(out_prefix_utf8),
                           cfg = *config] { runJob(exporter, ctl, in, prefix, cfg); });
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_exporter_get_status(tc_exporter* exporter, tc_export_status* out_status) {
  if (exporter == nullptr || out_status == nullptr) return TC_ERR_INVALID_ARG;
  if (out_status->struct_size != sizeof(tc_export_status)) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(exporter->mutex);
  tc_export_status s{};
  s.struct_size = sizeof(tc_export_status);
  s.state = exporter->state;
  s.result = exporter->result;
  if (const auto& c = exporter->control) {
    s.progress = c->progress.load();
    s.chunks_done = c->chunksDone.load();
    s.chunks_total = c->chunksTotal.load();
    s.sample_rate = c->sampleRate.load();
    s.channels = c->channels.load();
    s.frames = c->frames.load();
    s.chunk_frames = c->chunkFrames.load();
  }
  *out_status = s;
  return TC_OK;
}

tc_result tc_exporter_cancel(tc_exporter* exporter) {
  if (exporter == nullptr) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(exporter->mutex);
  if (exporter->control) exporter->control->cancelled.store(true);
  return TC_OK;
}

void tc_exporter_destroy(tc_exporter* exporter) {
  if (exporter == nullptr) return;
  try {
    tc_exporter_cancel(exporter);
    delete exporter;  // Worker join luồng nền (dừng ở lần kiểm tra cờ huỷ kế tiếp)
  } catch (...) {
    // Không để exception vượt ranh giới C.
  }
}

}  // extern "C"
