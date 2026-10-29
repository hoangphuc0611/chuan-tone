// Lớp C API cho phân tích bài (tc_analyzer_*, từ v0.4) — xem docs/adr/003-song-analyzer-api.md.
//
// Mỗi analyzer có một Worker (luồng nền) riêng. Trạng thái công việc hiện tại nằm trong
// AnalysisControl (atomic: tiến độ, giai đoạn, cờ huỷ); kết quả ghi dưới mutex khi xong. UI kéo
// trạng thái bằng tc_analyzer_get_status — không có callback nào gọi ngược vào Dart.

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "core/worker.h"
#include "decode/stems.h"
#include "key/song_analyzer.h"
#include "tonecore/tonecore.h"

struct tc_analyzer {
  std::mutex mutex;
  // Công việc hiện tại; mỗi lần start một đối tượng mới → cờ huỷ của lần trước không ảnh hưởng.
  std::shared_ptr<tonecore::AnalysisControl> control;  // bảo vệ bởi mutex
  int32_t state = TC_ANALYSIS_IDLE;                    // bảo vệ bởi mutex
  tc_result result = TC_OK;                            // bảo vệ bởi mutex
  tonecore::SongAnalysis analysis;                     // bảo vệ bởi mutex
  // Khai báo cuối để bị huỷ đầu tiên: join luồng nền trước khi các thành viên khác mất.
  tonecore::Worker worker;
};

namespace {

int32_t scaleOf(tonecore::KeyMode mode) {
  return mode == tonecore::KeyMode::Minor ? TC_SCALE_NATURAL_MINOR : TC_SCALE_MAJOR;
}

void runJob(tc_analyzer* a, const std::shared_ptr<tonecore::AnalysisControl>& ctl,
            const std::string& path, const std::vector<std::string>& vocals) {
  tonecore::SongAnalysis result;
  tc_result r = TC_ERR_INTERNAL;
  try {
    if (vocals.empty()) {
      r = tonecore::analyzeFile(path, tonecore::AnalysisConfig{}, *ctl, result);
    } else {
      // Phần nhạc = bài − giọng (M6): dò tone không bị giọng ca sĩ làm nhiễu.
      ctl->report(tonecore::AnalysisStage::Decoding, 0.0f);
      tonecore::DecodedAudio instrumental;
      tonecore::DecodedAudio vocal;
      r = tonecore::decodeStems(path, vocals, instrumental, vocal);
      vocal = tonecore::DecodedAudio{};
      if (r == TC_OK) {
        r = tonecore::analyzeDecoded(std::move(instrumental), tonecore::AnalysisConfig{}, *ctl,
                                     result);
      }
    }
  } catch (const std::bad_alloc&) {
    r = TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    r = TC_ERR_INTERNAL;
  }
  std::lock_guard<std::mutex> lock(a->mutex);
  a->result = r;
  a->state = r == TC_OK                        ? TC_ANALYSIS_DONE
             : r == tonecore::kResultCancelled ? TC_ANALYSIS_CANCELLED
                                               : TC_ANALYSIS_FAILED;
  if (r == TC_OK) a->analysis = std::move(result);
}

}  // namespace

namespace {

tc_result startJob(tc_analyzer* analyzer, const char* path_utf8, std::vector<std::string> vocals) {
  if (analyzer == nullptr || path_utf8 == nullptr || path_utf8[0] == '\0') {
    return TC_ERR_INVALID_ARG;
  }
  std::lock_guard<std::mutex> lock(analyzer->mutex);
  if (analyzer->state == TC_ANALYSIS_RUNNING) return TC_ERR_INVALID_STATE;
  auto ctl = std::make_shared<tonecore::AnalysisControl>();
  analyzer->control = ctl;
  analyzer->state = TC_ANALYSIS_RUNNING;
  analyzer->result = TC_OK;
  analyzer->analysis = tonecore::SongAnalysis{};
  analyzer->worker.post([analyzer, ctl, path = std::string(path_utf8), v = std::move(vocals)] {
    runJob(analyzer, ctl, path, v);
  });
  return TC_OK;
}

}  // namespace

extern "C" {

tc_result tc_analyzer_create(tc_analyzer** out_analyzer) {
  if (out_analyzer == nullptr) return TC_ERR_INVALID_ARG;
  *out_analyzer = nullptr;
  try {
    *out_analyzer = new tc_analyzer();
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
  return TC_OK;
}

tc_result tc_analyzer_start(tc_analyzer* analyzer, const char* path_utf8) {
  try {
    return startJob(analyzer, path_utf8, {});
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_analyzer_start_with_vocals(tc_analyzer* analyzer, const char* path_utf8,
                                        const char* const* vocal_paths_utf8, uint32_t vocal_count) {
  if (vocal_paths_utf8 == nullptr || vocal_count == 0 || vocal_count > 4096) {
    return TC_ERR_INVALID_ARG;
  }
  try {
    std::vector<std::string> vocals;
    for (uint32_t i = 0; i < vocal_count; ++i) {
      if (vocal_paths_utf8[i] == nullptr || vocal_paths_utf8[i][0] == '\0') {
        return TC_ERR_INVALID_ARG;
      }
      vocals.emplace_back(vocal_paths_utf8[i]);
    }
    return startJob(analyzer, path_utf8, std::move(vocals));
  } catch (const std::bad_alloc&) {
    return TC_ERR_OUT_OF_MEMORY;
  } catch (...) {
    return TC_ERR_INTERNAL;
  }
}

tc_result tc_analyzer_get_status(tc_analyzer* analyzer, tc_analysis_status* out_status) {
  if (analyzer == nullptr || out_status == nullptr) return TC_ERR_INVALID_ARG;
  if (out_status->struct_size != sizeof(tc_analysis_status)) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(analyzer->mutex);
  out_status->state = analyzer->state;
  out_status->result = analyzer->result;
  if (analyzer->control) {
    out_status->stage = static_cast<int32_t>(analyzer->control->stage());
    out_status->progress = analyzer->control->progress();
  } else {
    out_status->stage = TC_STAGE_NONE;
    out_status->progress = 0.0f;
  }
  return TC_OK;
}

tc_result tc_analyzer_cancel(tc_analyzer* analyzer) {
  if (analyzer == nullptr) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(analyzer->mutex);
  if (analyzer->control) analyzer->control->cancel();
  return TC_OK;
}

tc_result tc_analyzer_get_result(tc_analyzer* analyzer, tc_analysis_result* out_result) {
  if (analyzer == nullptr || out_result == nullptr) return TC_ERR_INVALID_ARG;
  if (out_result->struct_size != sizeof(tc_analysis_result)) return TC_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(analyzer->mutex);
  if (analyzer->state != TC_ANALYSIS_DONE) return TC_ERR_INVALID_STATE;
  const tonecore::SongAnalysis& a = analyzer->analysis;
  tc_analysis_result r{};
  r.struct_size = sizeof(tc_analysis_result);
  r.duration_ms = a.durationSec * 1000.0;
  r.sample_rate = a.sourceRate;
  r.channels = a.channels;
  r.has_key = a.key ? 1 : 0;
  r.key_root = a.key ? a.key->root : 0;
  r.key_scale = a.key ? scaleOf(a.key->mode) : TC_SCALE_MAJOR;
  r.key_confidence = a.keyConfidence;
  r.has_tuning = a.tuningCents ? 1 : 0;
  r.tuning_cents = a.tuningCents.value_or(0.0f);
  r.has_bpm = a.bpm ? 1 : 0;
  r.bpm = a.bpm.value_or(0.0f);
  r.bpm_confidence = a.bpmConfidence;
  r.has_loudness = a.integratedLufs ? 1 : 0;
  r.integrated_lufs = a.integratedLufs.value_or(0.0f);
  r.sample_peak_db = a.samplePeakDb.value_or(-200.0f);
  r.timeline_count = static_cast<uint32_t>(a.timeline.size());
  std::memcpy(out_result, &r, sizeof(r));
  return TC_OK;
}

int32_t tc_analyzer_get_key_timeline(tc_analyzer* analyzer, tc_key_segment* out_segments,
                                     uint32_t max_segments) {
  if (analyzer == nullptr || (out_segments == nullptr && max_segments > 0)) {
    return TC_ERR_INVALID_ARG;
  }
  std::lock_guard<std::mutex> lock(analyzer->mutex);
  if (analyzer->state != TC_ANALYSIS_DONE) return TC_ERR_INVALID_STATE;
  const auto& tl = analyzer->analysis.timeline;
  if (out_segments == nullptr) return static_cast<int32_t>(tl.size());
  const auto n = static_cast<uint32_t>(std::min<std::size_t>(tl.size(), max_segments));
  for (uint32_t i = 0; i < n; ++i) {
    tc_key_segment& s = out_segments[i];
    s.start_ms = tl[i].startSec * 1000.0;
    s.end_ms = tl[i].endSec * 1000.0;
    s.root = tl[i].key.root;
    s.scale = scaleOf(tl[i].key.mode);
    s.confidence = tl[i].confidence;
  }
  return static_cast<int32_t>(n);
}

void tc_analyzer_destroy(tc_analyzer* analyzer) {
  if (analyzer == nullptr) return;
  try {
    tc_analyzer_cancel(analyzer);
    delete analyzer;  // Worker join luồng nền (việc đang chạy dừng ở lần kiểm tra cờ huỷ kế tiếp)
  } catch (...) {
    // Không để exception vượt ranh giới C.
  }
}

}  // extern "C"
