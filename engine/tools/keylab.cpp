// keylab — công cụ thử dò tone / BPM / loudness trên file audio (chạy trên Mac/Linux).
//
//   keylab analyze <file> [tham số] [--chroma out.csv]
//                                                 → 1 dòng JSON kết quả phân tích; --chroma ghi
//                                                   chroma từng khung (time, energy_db, bass_*,
//                                                   treble_*) để xem bằng bảng tính / matplotlib
//   keylab batch [-j N] [tham số] < danh_sach      → JSONL, mỗi dòng một bài (thứ tự xong trước
//                                                    in trước; trường "file" giữ nguyên đường dẫn)
//
// `tools/keytest/keytest.py` gọi `keylab batch` để chấm điểm MIREX trên bộ test.
// Định dạng JSON (trường chưa dò được = null):
//   {"file", "status": "ok"|"error", "error", "duration_sec", "sample_rate", "channels",
//    "analysis_rate",
//    "key": {"root", "mode", "name", "confidence"} | null,
//    "timeline": [{"start", "end", "root", "mode", "confidence"}],
//    "tuning_cents", "bpm", "bpm_confidence", "lufs", "peak_db",
//    "timing_ms": {"decode", "resample", …, "total"}}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "key/song_analyzer.h"

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(stderr,
               "Cách dùng:\n"
               "  keylab analyze <file> [tham số] [--chroma out.csv]\n"
               "  keylab batch [-j N] [tham số] < danh_sach_file.txt\n"
               "Tham số: --rate 11025  --profile kk|temperley|temperley-kp|diatonic\n"
               "         --bass-weight 0.25  --frame-norm none|l1|max  --silence-db 40\n"
               "         --compression 100\n"
               "Timeline: --timeline on|off  --window-sec 6  --switch-prob 0.002\n"
               "          --related-boost 10  --min-segment 12  --emission-temp 0.1\n");
  return 2;
}

const char* const kRootNames[12] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                    "F#", "G",  "Ab", "A",  "Bb", "B"};

const char* modeName(KeyMode m) {
  return m == KeyMode::Minor ? "minor" : "major";
}

const char* stageName(AnalysisStage s) {
  switch (s) {
    case AnalysisStage::Decoding:
      return "decode";
    case AnalysisStage::Resampling:
      return "resample";
    case AnalysisStage::Separating:
      return "hpss";
    case AnalysisStage::Spectrum:
      return "cqt";
    case AnalysisStage::Key:
      return "key";
    case AnalysisStage::Idle:
    case AnalysisStage::Done:
      break;
  }
  return "other";
}

const char* resultName(tc_result r) {
  return r == kResultCancelled ? "TC_ERR_CANCELLED" : tc_result_to_string(r);
}

// Chuỗi JSON: thoát ", \ và ký tự điều khiển; UTF-8 (tên bài tiếng Việt) giữ nguyên.
void appendString(std::string& o, const std::string& s) {
  o += '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') {
      o += '\\';
      o += ch;
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      o += buf;
    } else {
      o += ch;
    }
  }
  o += '"';
}

void appendNumber(std::string& o, double v) {
  if (!std::isfinite(v)) {
    o += "null";
    return;
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  o += buf;
}

template <typename T>
void appendOptional(std::string& o, const std::optional<T>& v) {
  if (v) {
    appendNumber(o, static_cast<double>(*v));
  } else {
    o += "null";
  }
}

std::string toJson(const std::string& file, tc_result r, const SongAnalysis& a, double totalMs) {
  std::string o = "{\"file\":";
  appendString(o, file);
  o += ",\"status\":";
  o += r == TC_OK ? "\"ok\"" : "\"error\"";
  if (r != TC_OK) {
    o += ",\"error\":";
    appendString(o, resultName(r));
  }
  o += ",\"duration_sec\":";
  appendNumber(o, a.durationSec);
  o += ",\"sample_rate\":";
  appendNumber(o, a.sourceRate);
  o += ",\"channels\":";
  appendNumber(o, a.channels);
  o += ",\"analysis_rate\":";
  appendNumber(o, a.analysisRate);

  o += ",\"key\":";
  if (r == TC_OK && a.key) {
    o += "{\"root\":";
    appendNumber(o, a.key->root);
    o += ",\"mode\":\"";
    o += modeName(a.key->mode);
    o += "\",\"name\":\"";
    o += kRootNames[a.key->root % 12];
    if (a.key->mode == KeyMode::Minor) o += 'm';
    o += "\",\"confidence\":";
    appendNumber(o, a.keyConfidence);
    o += '}';
  } else {
    o += "null";
  }

  o += ",\"timeline\":[";
  for (std::size_t i = 0; r == TC_OK && i < a.timeline.size(); ++i) {
    const KeySegment& s = a.timeline[i];
    if (i > 0) o += ',';
    o += "{\"start\":";
    appendNumber(o, s.startSec);
    o += ",\"end\":";
    appendNumber(o, s.endSec);
    o += ",\"root\":";
    appendNumber(o, s.key.root);
    o += ",\"mode\":\"";
    o += modeName(s.key.mode);
    o += "\",\"confidence\":";
    appendNumber(o, s.confidence);
    o += '}';
  }
  o += ']';

  const bool ok = r == TC_OK;
  o += ",\"tuning_cents\":";
  appendOptional(o, ok ? a.tuningCents : std::nullopt);
  o += ",\"bpm\":";
  appendOptional(o, ok ? a.bpm : std::nullopt);
  o += ",\"bpm_confidence\":";
  if (ok && a.bpm) {
    appendNumber(o, a.bpmConfidence);
  } else {
    o += "null";
  }
  o += ",\"lufs\":";
  appendOptional(o, ok ? a.integratedLufs : std::nullopt);
  o += ",\"peak_db\":";
  appendOptional(o, ok ? a.samplePeakDb : std::nullopt);

  o += ",\"timing_ms\":{";
  for (const StageTiming& t : a.timings) {
    o += '"';
    o += stageName(t.stage);
    o += "\":";
    appendNumber(o, t.ms);
    o += ',';
  }
  o += "\"total\":";
  appendNumber(o, totalMs);
  o += "}}";
  return o;
}

bool writeChromaCsv(const std::string& path, const Chromagram& ch) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (f == nullptr) return false;
  std::fputs("time,energy_db", f);
  for (const char* prefix : {"bass", "treble"}) {
    for (const char* name : kRootNames) std::fprintf(f, ",%s_%s", prefix, name);
  }
  std::fputc('\n', f);
  for (std::size_t i = 0; i < ch.frames(); ++i) {
    std::fprintf(f, "%.3f,%.2f", static_cast<double>(i) * ch.frameSec,
                 static_cast<double>(ch.energyDb[i]));
    for (const float v : ch.bass[i]) std::fprintf(f, ",%.4f", static_cast<double>(v));
    for (const float v : ch.treble[i]) std::fprintf(f, ",%.4f", static_cast<double>(v));
    std::fputc('\n', f);
  }
  return std::fclose(f) == 0;
}

std::string analyzeToJson(const std::string& file, const AnalysisConfig& cfg,
                          const std::string& chromaCsv = {}) {
  const auto t0 = std::chrono::steady_clock::now();
  AnalysisControl ctl;
  SongAnalysis a;
  const tc_result r = analyzeFile(file, cfg, ctl, a);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (r == TC_OK && !chromaCsv.empty() && !writeChromaCsv(chromaCsv, a.chroma)) {
    std::fprintf(stderr, "Không ghi được %s\n", chromaCsv.c_str());
  }
  return toJson(file, r, a, ms);
}

// Đọc các tham số phân tích chung (keytest sweep truyền vào đây). Trả false nếu có tham số lạ.
bool parseConfigArg(int argc, char** argv, int& i, AnalysisConfig& cfg) {
  const std::string opt = argv[i];
  if (i + 1 >= argc) return false;
  const std::string val = argv[++i];
  if (opt == "--rate") {
    cfg.targetAnalysisRate = std::stod(val);
    return cfg.targetAnalysisRate >= 4000.0 && cfg.targetAnalysisRate <= 48000.0;
  }
  if (opt == "--profile") {
    cfg.key.profile = val;
    return findKeyProfile(val) != nullptr;
  }
  if (opt == "--bass-weight") {
    cfg.key.bassWeight = std::stof(val);
    return cfg.key.bassWeight >= 0.0f && cfg.key.bassWeight <= 1.0f;
  }
  if (opt == "--frame-norm") {
    if (val == "none") {
      cfg.key.frameNorm = ChromaFrameNorm::None;
    } else if (val == "l1") {
      cfg.key.frameNorm = ChromaFrameNorm::L1;
    } else if (val == "max") {
      cfg.key.frameNorm = ChromaFrameNorm::Max;
    } else {
      return false;
    }
    return true;
  }
  if (opt == "--silence-db") {
    cfg.key.silenceDb = std::stof(val);
    return cfg.key.silenceDb > 0.0f;
  }
  if (opt == "--timeline") {
    if (val != "on" && val != "off") return false;
    cfg.timeline.enabled = val == "on";
    return true;
  }
  if (opt == "--window-sec") {
    cfg.timeline.windowSec = std::stof(val);
    return cfg.timeline.windowSec >= 1.0f;
  }
  if (opt == "--switch-prob") {
    cfg.timeline.switchProb = std::stof(val);
    return cfg.timeline.switchProb > 0.0f && cfg.timeline.switchProb < 0.5f;
  }
  if (opt == "--related-boost") {
    cfg.timeline.relatedBoost = std::stof(val);
    return cfg.timeline.relatedBoost >= 1.0f;
  }
  if (opt == "--min-segment") {
    cfg.timeline.minSegmentSec = std::stof(val);
    return cfg.timeline.minSegmentSec >= 0.0f;
  }
  if (opt == "--emission-temp") {
    cfg.timeline.emissionTemperature = std::stof(val);
    return cfg.timeline.emissionTemperature > 0.0f;
  }
  if (opt == "--compression") {
    cfg.chroma.compression = std::stof(val);
    return cfg.chroma.compression > 0.0f;
  }
  return false;
}

int cmdAnalyze(int argc, char** argv) {
  if (argc < 3) return usage();
  AnalysisConfig cfg;
  std::string chromaCsv;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--chroma") == 0 && i + 1 < argc) {
      chromaCsv = argv[++i];
    } else if (!parseConfigArg(argc, argv, i, cfg)) {
      return usage();
    }
  }
  const std::string json = analyzeToJson(argv[2], cfg, chromaCsv);
  std::printf("%s\n", json.c_str());
  return json.find("\"status\":\"ok\"") != std::string::npos ? 0 : 1;
}

int cmdBatch(int argc, char** argv) {
  AnalysisConfig cfg;
  unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
      const int j = std::stoi(argv[++i]);
      if (j < 1) return usage();
      jobs = static_cast<unsigned>(j);
    } else if (!parseConfigArg(argc, argv, i, cfg)) {
      return usage();
    }
  }

  std::vector<std::string> files;
  for (std::string line; std::getline(std::cin, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) files.push_back(line);
  }

  // Mỗi luồng lấy bài tiếp theo; in ngay khi xong để keytest hiện tiến độ.
  std::atomic<std::size_t> next{0};
  std::mutex outMutex;
  auto worker = [&] {
    for (std::size_t i = next++; i < files.size(); i = next++) {
      const std::string json = analyzeToJson(files[i], cfg);
      const std::lock_guard<std::mutex> lock(outMutex);
      std::fputs(json.c_str(), stdout);
      std::fputc('\n', stdout);
      std::fflush(stdout);
    }
  };
  std::vector<std::thread> pool;
  const auto n = std::min<std::size_t>(jobs, std::max<std::size_t>(1, files.size()));
  for (std::size_t t = 0; t < n; ++t) pool.emplace_back(worker);
  for (auto& t : pool) t.join();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "analyze") return cmdAnalyze(argc, argv);
  if (cmd == "batch") return cmdBatch(argc, argv);
  return usage();
}
