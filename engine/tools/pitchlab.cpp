// pitchlab — công cụ thử dò cao độ / chỉnh giọng trên file audio (chạy trên Mac/Linux).
//
//   pitchlab f0 <file> [--algo mpm|yin] [ngưỡng…]  → CSV: time,f0,voiced,clarity,raw_f0
//
// File được giải mã bằng codec của hệ điều hành (macOS: WAV/MP3/M4A; Linux: WAV/MP3), trộn về
// mono và đưa thẳng vào PitchAnalyzer ở tần số gốc của file.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "pitch/pitch_analyzer.h"

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(stderr,
               "Cách dùng:\n"
               "  pitchlab f0 <file> [--algo mpm|yin] [--silence-db -55] [--clarity-on 0.8]\n"
               "                    [--clarity-off 0.6]\n");
  return 2;
}

bool loadMono(const std::string& path, std::vector<float>& mono, double& rate) {
  DecodedAudio a;
  const tc_result r = decodeFile(path, a);
  if (r != TC_OK) {
    std::fprintf(stderr, "Không giải mã được %s (mã %d)\n", path.c_str(), r);
    return false;
  }
  mono.resize(a.frames);
  for (uint64_t i = 0; i < a.frames; ++i) {
    float acc = 0.0f;
    for (uint32_t c = 0; c < a.channels; ++c) {
      acc += static_cast<float>(a.samples[i * a.channels + c]) / 32768.0f;
    }
    mono[i] = acc / static_cast<float>(a.channels);
  }
  rate = a.sampleRate;
  return true;
}

int cmdF0(int argc, char** argv) {
  if (argc < 3) return usage();
  PitchAnalyzerConfig cfg;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--algo") == 0 && i + 1 < argc) {
      const std::string a = argv[++i];
      if (a == "yin") {
        cfg.algorithm = PitchAlgorithm::Yin;
      } else if (a != "mpm") {
        return usage();
      }
    } else if (std::strcmp(argv[i], "--silence-db") == 0 && i + 1 < argc) {
      cfg.tracker.silenceDb = std::stof(argv[++i]);
    } else if (std::strcmp(argv[i], "--clarity-on") == 0 && i + 1 < argc) {
      cfg.tracker.clarityOn = std::stof(argv[++i]);
    } else if (std::strcmp(argv[i], "--clarity-off") == 0 && i + 1 < argc) {
      cfg.tracker.clarityOff = std::stof(argv[++i]);
    } else {
      return usage();
    }
  }
  std::vector<float> mono;
  double rate = 0.0;
  if (!loadMono(argv[2], mono, rate)) return 1;
  cfg.inputRate = rate;
  PitchAnalyzer pa(cfg);
  std::printf("time,f0,voiced,clarity,raw_f0\n");
  pa.process(mono.data(), mono.size(), [](const PitchFrame& f) {
    std::printf("%.5f,%.3f,%d,%.3f,%.3f\n", f.timeSec, f.tracked.voiced ? f.tracked.f0 : 0.0f,
                f.tracked.voiced ? 1 : 0, f.raw.clarity, f.raw.f0);
  });
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "f0") return cmdF0(argc, argv);
  return usage();
}
