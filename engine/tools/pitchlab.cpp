// pitchlab — công cụ thử dò cao độ / chỉnh giọng trên file audio (chạy trên Mac/Linux).
//
//   pitchlab f0 <file> [--algo mpm|yin] [ngưỡng…]  → CSV: time,f0,voiced,clarity,raw_f0
//   pitchlab process <file> --key Am --preset natural|singer|robot -o out.wav
//   pitchlab ab <file> --key Am -o <thư mục>     → khô / tự nhiên / ca sĩ / robot (nghe A/B)
//
// Tone: tên nốt quốc tế C, C#, Db … B; thêm "m" cho thứ tự nhiên (Am, F#m). Muốn thang khác dùng
// --scale major|minor|harmonic|penta|pentam|chromatic.
//
// File được giải mã bằng codec của hệ điều hành (macOS: WAV/MP3/M4A; Linux: WAV/MP3), trộn về
// mono và đưa thẳng vào PitchAnalyzer ở tần số gốc của file.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "pitch/pitch_analyzer.h"
#include "pitch/voice_pitch_node.h"

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(stderr,
               "Cách dùng:\n"
               "  pitchlab f0 <file> [--algo mpm|yin] [--silence-db -45] [--clarity-on 0.8]\n"
               "                    [--clarity-off 0.75]\n"
               "  pitchlab process <file> --key Am [--scale minor] --preset natural|singer|robot\n"
               "                   -o out.wav\n"
               "  pitchlab ab <file> --key Am [--scale minor] -o <thư mục>\n");
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

// Ghi WAV PCM 16-bit mono.
bool writeWav(const std::string& path, const std::vector<float>& x, uint32_t rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const auto bytes = static_cast<uint32_t>(x.size() * 2);
  auto u32 = [&](uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                static_cast<unsigned char>(v >> 16),
                                static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  auto u16 = [&](uint16_t v) {
    const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
  };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(1);
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(bytes);
  for (float v : x) {
    const float c = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    const auto s = static_cast<int16_t>(c * 32767.0f);
    u16(static_cast<uint16_t>(s));
  }
  return std::fclose(f) == 0;
}

// "Am" → (9, thứ tự nhiên); "C#" → (1, trưởng). Trả false nếu không hiểu.
bool parseKey(const std::string& k, int& root, Scale& scale) {
  if (k.empty()) return false;
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  const char c = static_cast<char>(k[0] >= 'a' ? k[0] - 32 : k[0]);
  if (c < 'A' || c > 'G') return false;
  root = base[c - 'A'];
  std::size_t i = 1;
  if (i < k.size() && k[i] == '#') {
    ++root;
    ++i;
  } else if (i < k.size() && k[i] == 'b') {
    --root;
    ++i;
  }
  root = (root + 12) % 12;
  const std::string rest = k.substr(i);
  if (rest.empty()) {
    scale = Scale::Major;
  } else if (rest == "m") {
    scale = Scale::NaturalMinor;
  } else {
    return false;
  }
  return true;
}

bool parseScale(const std::string& s, Scale& scale) {
  if (s == "major")
    scale = Scale::Major;
  else if (s == "minor")
    scale = Scale::NaturalMinor;
  else if (s == "harmonic")
    scale = Scale::HarmonicMinor;
  else if (s == "penta")
    scale = Scale::PentatonicMajor;
  else if (s == "pentam")
    scale = Scale::PentatonicMinor;
  else if (s == "chromatic")
    scale = Scale::Chromatic;
  else
    return false;
  return true;
}

bool parsePreset(const std::string& s, CorrectionPreset& p) {
  if (s == "natural")
    p = CorrectionPreset::Natural;
  else if (s == "singer")
    p = CorrectionPreset::Singer;
  else if (s == "robot")
    p = CorrectionPreset::Robot;
  else
    return false;
  return true;
}

// Chạy khối Tự chỉnh giọng trên cả file (block 256 như trên máy thật). enabled=false → chỉ HPF.
std::vector<float> render(const std::vector<float>& in, double rate, int root, Scale scale,
                          bool enabled, CorrectionPreset preset) {
  constexpr uint32_t kBlock = 256;
  VoicePitchNode node(rate, kBlock);
  node.setKey(root, scale);
  node.applyPreset(preset);
  node.setEnabled(enabled);
  std::vector<float> out = in;
  for (std::size_t off = 0; off < out.size(); off += kBlock) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(kBlock, out.size() - off));
    node.process(out.data() + off, n);
  }
  return out;
}

struct ProcessArgs {
  std::string input;
  std::string output;
  int root = -1;
  Scale scale = Scale::Major;
  bool scaleGiven = false;
  CorrectionPreset preset = CorrectionPreset::Singer;
};

bool parseProcessArgs(int argc, char** argv, ProcessArgs& a, bool needPreset) {
  if (argc < 3) return false;
  a.input = argv[2];
  bool presetGiven = false;
  Scale keyScale = Scale::Major;
  for (int i = 3; i < argc; ++i) {
    const std::string opt = argv[i];
    if (i + 1 >= argc) return false;
    const std::string val = argv[++i];
    if (opt == "--key") {
      if (!parseKey(val, a.root, keyScale)) return false;
    } else if (opt == "--scale") {
      if (!parseScale(val, a.scale)) return false;
      a.scaleGiven = true;
    } else if (opt == "--preset") {
      if (!parsePreset(val, a.preset)) return false;
      presetGiven = true;
    } else if (opt == "-o") {
      a.output = val;
    } else {
      return false;
    }
  }
  if (!a.scaleGiven) a.scale = keyScale;
  return a.root >= 0 && !a.output.empty() && (presetGiven || !needPreset);
}

int cmdProcess(int argc, char** argv) {
  ProcessArgs a;
  if (!parseProcessArgs(argc, argv, a, true)) return usage();
  std::vector<float> mono;
  double rate = 0.0;
  if (!loadMono(a.input, mono, rate)) return 1;
  const auto out = render(mono, rate, a.root, a.scale, true, a.preset);
  if (!writeWav(a.output, out, static_cast<uint32_t>(rate))) {
    std::fprintf(stderr, "Không ghi được %s\n", a.output.c_str());
    return 1;
  }
  return 0;
}

int cmdAb(int argc, char** argv) {
  ProcessArgs a;
  if (!parseProcessArgs(argc, argv, a, false)) return usage();
  std::vector<float> mono;
  double rate = 0.0;
  if (!loadMono(a.input, mono, rate)) return 1;
  const auto r = static_cast<uint32_t>(rate);
  struct Variant {
    const char* name;
    bool enabled;
    CorrectionPreset preset;
  };
  const Variant variants[] = {{"0_kho", false, CorrectionPreset::Singer},
                              {"1_tu_nhien", true, CorrectionPreset::Natural},
                              {"2_ca_si", true, CorrectionPreset::Singer},
                              {"3_robot", true, CorrectionPreset::Robot}};
  for (const auto& v : variants) {
    const std::string path = a.output + "/" + v.name + ".wav";
    if (!writeWav(path, render(mono, rate, a.root, a.scale, v.enabled, v.preset), r)) {
      std::fprintf(stderr, "Không ghi được %s\n", path.c_str());
      return 1;
    }
    std::printf("%s\n", path.c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "f0") return cmdF0(argc, argv);
  if (cmd == "process") return cmdProcess(argc, argv);
  if (cmd == "ab") return cmdAb(argc, argv);
  return usage();
}
