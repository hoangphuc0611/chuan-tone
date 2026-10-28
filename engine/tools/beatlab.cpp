// beatlab — công cụ thử đổi tone beat (M4), chạy trên Mac/Linux.
//
//   beatlab render <file> -o out.wav --semitones N [--algo pv|pv-basic|signalsmith] [--start giây]
//                  [--dur giây] [--fft 4096] [--hop 1024]
//        → WAV 16-bit cùng tần số lấy mẫu với file vào; in 1 dòng JSON (thời gian render). Có
//          limiter đỉnh như engine (--limiter off để tắt).
//   beatlab eval [--algo pv,pv-basic,signalsmith] [--levels -6,-4,-2,2,4,6] [--fft 4096]
//                [--hop 1024]
//        → bảng markdown các chỉ số khách quan trên bộ tín hiệu tổng hợp
//          (tests/util/stretch_eval.h), kèm hàng "gốc" (chưa đổi tone) để đọc số tuyệt đối.
//   beatlab suite <thư mục> [--algo …] [--levels …]
//        → ghi bộ tín hiệu tổng hợp (…_goc.wav) và các bản đã đổi tone ra WAV.
//
// "pv" = PitchShifter của ToneCore (mặc định hiện tại); "pv-b2" = khoá pha, chưa giữ transient
// (B2); "pv-basic" = phase vocoder cơ bản B1 (mốc dưới); "signalsmith" = mốc so sánh (chỉ có khi
// build kèm, xem tools/stretch/README.md).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "stretch/peak_limiter.h"
#include "stretch/pitch_shifter.h"
#include "util/stretch_eval.h"
#include "util/wav_out.h"
#ifdef TC_HAVE_SIGNALSMITH
#include "signalsmith_ref.h"
#endif

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(stderr,
               "Cách dùng:\n"
               "  beatlab render <file> -o out.wav --semitones N [--algo pv|pv-basic|signalsmith]\n"
               "                 [--start giây] [--dur giây] [--fft 4096] [--hop 1024]\n"
               "  (cả ba lệnh) --transients on|off  --anchor-up 0.5  --anchor-down 1\n"
               "  beatlab eval [--algo pv,signalsmith] [--levels -6,-4,-2,2,4,6]\n"
               "               [--fft 4096] [--hop 1024]\n"
               "  beatlab suite <thư mục> [--algo pv,signalsmith] [--levels -6,…]\n");
  return 2;
}

bool haveSignalsmith() {
#ifdef TC_HAVE_SIGNALSMITH
  return true;
#else
  return false;
#endif
}

// Đổi cao độ cả tín hiệu bằng thuật toán `algo`. Trả rỗng nếu thuật toán không có.
std::vector<float> shift(const std::string& algo, const float* in, uint64_t frames, int channels,
                         double rate, double semitones, const PitchShiftConfig& cfg) {
  if (algo == "pv") return pitchShiftBuffer(in, frames, channels, semitones, cfg);
  if (algo == "pv-b2") {
    PitchShiftConfig b2 = cfg;
    b2.transients = false;
    return pitchShiftBuffer(in, frames, channels, semitones, b2);
  }
  if (algo == "pv-basic") {
    PitchShiftConfig basic = cfg;
    basic.phaseLock = false;
    return pitchShiftBuffer(in, frames, channels, semitones, basic);
  }
#ifdef TC_HAVE_SIGNALSMITH
  if (algo == "signalsmith") return signalsmithShift(in, frames, channels, rate, semitones);
#endif
  (void)rate;
  return {};
}

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  std::size_t a = 0;
  while (a <= s.size()) {
    const std::size_t b = s.find(',', a);
    const std::string item = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
    if (!item.empty()) out.push_back(item);
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}

double seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

int cmdRender(int argc, char** argv) {
  if (argc < 3) return usage();
  std::string in = argv[2];
  std::string out;
  std::string algo = "pv";
  double semitones = 0.0;
  double startSec = 0.0;
  double durSec = -1.0;
  bool limiter = true;
  PitchShiftConfig cfg;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    const bool hasVal = i + 1 < argc;
    if (a == "-o" && hasVal) {
      out = argv[++i];
    } else if (a == "--semitones" && hasVal) {
      semitones = std::atof(argv[++i]);
    } else if (a == "--algo" && hasVal) {
      algo = argv[++i];
    } else if (a == "--start" && hasVal) {
      startSec = std::atof(argv[++i]);
    } else if (a == "--dur" && hasVal) {
      durSec = std::atof(argv[++i]);
    } else if (a == "--limiter" && hasVal) {
      limiter = std::strcmp(argv[++i], "off") != 0;
    } else if (a == "--fft" && hasVal) {
      cfg.fftSize = std::atoi(argv[++i]);
    } else if (a == "--hop" && hasVal) {
      cfg.synthesisHop = std::atoi(argv[++i]);
    } else if (a == "--anchor-up" && hasVal) {
      cfg.anchorUp = std::atof(argv[++i]);
    } else if (a == "--anchor-down" && hasVal) {
      cfg.anchorDown = std::atof(argv[++i]);
    } else if (a == "--transients" && hasVal) {
      cfg.transients = std::strcmp(argv[++i], "off") != 0;
    } else {
      return usage();
    }
  }
  if (out.empty()) return usage();
  DecodedAudio dec;
  if (decodeFile(in, dec) != TC_OK || dec.frames == 0) {
    std::fprintf(stderr, "Không giải mã được %s\n", in.c_str());
    return 1;
  }
  const int ch = static_cast<int>(dec.channels);
  cfg.sampleRate = dec.sampleRate;
  const auto first =
      std::min<uint64_t>(dec.frames, static_cast<uint64_t>(startSec * dec.sampleRate));
  uint64_t frames = dec.frames - first;
  if (durSec > 0.0)
    frames = std::min<uint64_t>(frames, static_cast<uint64_t>(durSec * dec.sampleRate));
  std::vector<float> x(static_cast<std::size_t>(frames) * static_cast<std::size_t>(ch));
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = static_cast<float>(
               dec.samples[static_cast<std::size_t>(first) * static_cast<std::size_t>(ch) + i]) /
           32768.0f;
  }
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<float> y = shift(algo, x.data(), frames, ch, dec.sampleRate, semitones, cfg);
  const auto t1 = std::chrono::steady_clock::now();
  if (y.empty()) {
    std::fprintf(stderr, "Không có thuật toán \"%s\"%s\n", algo.c_str(),
                 algo == "signalsmith" ? " (chưa build kèm Signalsmith)" : "");
    return 1;
  }
  // Như engine (BeatRenderer): giới hạn đỉnh vượt 0 dBFS sau đổi pha — áp cho mọi thuật toán (so
  // sánh công bằng). Mức 0 (bản gốc) giữ nguyên.
  if (limiter && semitones != 0.0) limitPeaks(y, ch, dec.sampleRate);
  if (!test::writeWav(out, y, ch, dec.sampleRate)) {
    std::fprintf(stderr, "Không ghi được %s\n", out.c_str());
    return 1;
  }
  const double audioSec = static_cast<double>(frames) / dec.sampleRate;
  std::printf(
      "{\"file\": \"%s\", \"out\": \"%s\", \"algo\": \"%s\", \"semitones\": %g, "
      "\"duration_sec\": %.3f, \"song_sec\": %.3f, \"render_sec\": %.3f, "
      "\"realtime_x\": %.1f}\n",
      in.c_str(), out.c_str(), algo.c_str(), semitones, audioSec,
      static_cast<double>(dec.frames) / dec.sampleRate, seconds(t0, t1),
      audioSec / std::max(1e-9, seconds(t0, t1)));
  return 0;
}

std::string fmt(double v, const char* spec = "%.1f") {
  if (std::isnan(v)) return "—";
  char buf[32];
  std::snprintf(buf, sizeof buf, spec, v);
  return buf;
}

// Gộp chỉ số của cả bộ tín hiệu thành một hàng bảng.
struct Row {
  double sineCents = test::kNaN;
  double harmonicCents = test::kNaN;
  double offHarmonic = test::kNaN;
  double offChord = test::kNaN;
  test::StretchMetrics drums;
  test::StretchMetrics beat;
  double noiseDb = test::kNaN;
  double realtimeX = test::kNaN;
};

void printRow(const std::string& label, const Row& r) {
  std::printf("| %s | %s | %s | %s | %s | %s / %s | %s | %s | %s | %s | %s | %s | %s |\n",
              label.c_str(), fmt(r.sineCents, "%.2f").c_str(), fmt(r.harmonicCents, "%.2f").c_str(),
              fmt(r.offHarmonic).c_str(), fmt(r.offChord).c_str(), fmt(r.drums.preEchoDb).c_str(),
              fmt(r.drums.preEchoWorstDb).c_str(), fmt(r.drums.riseDeltaMs, "%.2f").c_str(),
              fmt(r.drums.reboundDb).c_str(), fmt(r.drums.lagMaxMs, "%.2f").c_str(),
              fmt(r.beat.correlationDelta, "%.3f").c_str(), fmt(r.beat.lufsDelta, "%.2f").c_str(),
              fmt(r.noiseDb, "%.2f").c_str(), fmt(r.realtimeX, "%.0f").c_str());
}

void absorb(Row& row, const test::StretchSignal& sig, const test::StretchMetrics& m) {
  if (sig.name == "sine") row.sineCents = m.pitchErrorCents;
  if (sig.name == "harmonic") {
    row.harmonicCents = m.pitchErrorCents;
    row.offHarmonic = m.offPartialDb;
  }
  if (sig.name == "chord") row.offChord = m.offPartialDb;
  if (sig.name == "drums") row.drums = m;
  if (sig.name == "beat") row.beat = m;
  if (sig.name == "noise") row.noiseDb = m.noiseLevelDb;
}

// Ghi bộ tín hiệu tổng hợp và các bản đã đổi tone ra WAV (để nghe / soi phổ).
int cmdSuite(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string dir = argv[2];
  std::vector<std::string> algos = {"pv"};
  if (haveSignalsmith()) algos.push_back("signalsmith");
  std::vector<double> levels = {-6, -4, -2, 2, 4, 6};
  PitchShiftConfig cfg;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    const bool hasVal = i + 1 < argc;
    if (a == "--algo" && hasVal) {
      algos = splitList(argv[++i]);
    } else if (a == "--levels" && hasVal) {
      levels.clear();
      for (const auto& s : splitList(argv[++i])) levels.push_back(std::atof(s.c_str()));
    } else if (a == "--fft" && hasVal) {
      cfg.fftSize = std::atoi(argv[++i]);
    } else if (a == "--hop" && hasVal) {
      cfg.synthesisHop = std::atoi(argv[++i]);
    } else if (a == "--anchor-up" && hasVal) {
      cfg.anchorUp = std::atof(argv[++i]);
    } else if (a == "--anchor-down" && hasVal) {
      cfg.anchorDown = std::atof(argv[++i]);
    } else if (a == "--transients" && hasVal) {
      cfg.transients = std::strcmp(argv[++i], "off") != 0;
    } else {
      return usage();
    }
  }
  for (const auto& sig : test::stretchSuite(48000.0)) {
    const auto rate = static_cast<uint32_t>(sig.rate);
    if (!test::writeWav(dir + "/" + sig.name + "_goc.wav", sig.stereo, 2, rate)) {
      std::fprintf(stderr, "Không ghi được vào %s\n", dir.c_str());
      return 1;
    }
    for (const auto& algo : algos) {
      for (const double n : levels) {
        const auto y = shift(algo, sig.stereo.data(), sig.frames(), 2, sig.rate, n, cfg);
        if (y.empty()) continue;
        char name[128];
        std::snprintf(name, sizeof name, "/%s_%s_%+g.wav", sig.name.c_str(), algo.c_str(), n);
        test::writeWav(dir + name, y, 2, rate);
      }
    }
  }
  return 0;
}

int cmdEval(int argc, char** argv) {
  std::vector<std::string> algos = {"pv"};
  if (haveSignalsmith()) algos.push_back("signalsmith");
  std::vector<double> levels = {-6, -4, -2, 2, 4, 6};
  PitchShiftConfig cfg;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    const bool hasVal = i + 1 < argc;
    if (a == "--algo" && hasVal) {
      algos = splitList(argv[++i]);
    } else if (a == "--levels" && hasVal) {
      levels.clear();
      for (const auto& s : splitList(argv[++i])) levels.push_back(std::atof(s.c_str()));
    } else if (a == "--fft" && hasVal) {
      cfg.fftSize = std::atoi(argv[++i]);
    } else if (a == "--hop" && hasVal) {
      cfg.synthesisHop = std::atoi(argv[++i]);
    } else if (a == "--anchor-up" && hasVal) {
      cfg.anchorUp = std::atof(argv[++i]);
    } else if (a == "--anchor-down" && hasVal) {
      cfg.anchorDown = std::atof(argv[++i]);
    } else if (a == "--transients" && hasVal) {
      cfg.transients = std::strcmp(argv[++i], "off") != 0;
    } else {
      return usage();
    }
  }
  const auto suite = test::stretchSuite(48000.0);
  std::printf("PitchShifter: fft %d, hop %d\n\n", cfg.fftSize, cfg.synthesisHop);
  std::printf(
      "| Thuật toán, mức | Cao độ sin (cent) | Cao độ hoạ âm (cent) | Ngoài hoạ âm: nốt "
      "(dB) | Ngoài hoạ âm: hợp âm (dB) | Trước onset: trung vị / tệ nhất (dB) | Thời gian "
      "lên +ms | Dội sau đỉnh (dB) | Lệch onset (ms) | Δ tương quan L–R | Δ LUFS | Nhiễu so với "
      "lý tưởng (dB) | × thời "
      "gian thực |\n");
  std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
  Row ref;
  for (const auto& sig : suite) absorb(ref, sig, test::measureReference(sig));
  printRow("gốc (chưa đổi)", ref);
  for (const auto& algo : algos) {
    if (algo == "signalsmith" && !haveSignalsmith()) {
      std::fprintf(stderr, "Bỏ qua signalsmith: chưa build kèm\n");
      continue;
    }
    for (const double n : levels) {
      Row row;
      double audioSec = 0.0;
      double cpuSec = 0.0;
      for (const auto& sig : suite) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto y = shift(algo, sig.stereo.data(), sig.frames(), 2, sig.rate, n, cfg);
        cpuSec += seconds(t0, std::chrono::steady_clock::now());
        audioSec += static_cast<double>(sig.frames()) / sig.rate;
        absorb(row, sig, test::measureStretch(sig, y, n));
      }
      row.realtimeX = audioSec / std::max(1e-9, cpuSec);
      char label[64];
      std::snprintf(label, sizeof label, "%s %+g", algo.c_str(), n);
      printRow(label, row);
    }
  }
  std::printf(
      "\nHàng \"gốc\": cột trước onset, thời gian lên, dội, tương quan, LUFS là giá trị "
      "tuyệt đối của tín hiệu chưa đổi; các hàng khác là chênh so với gốc (trừ trước "
      "onset và dội: tuyệt đối).\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  if (std::strcmp(argv[1], "render") == 0) return cmdRender(argc, argv);
  if (std::strcmp(argv[1], "eval") == 0) return cmdEval(argc, argv);
  if (std::strcmp(argv[1], "suite") == 0) return cmdSuite(argc, argv);
  return usage();
}
