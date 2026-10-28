// fxlab — thử bộ FX giọng (M5) trên máy dev.
//
//   fxlab render <file> -o out.wav [--start giây] [--dur giây] [khối FX…]
//       Giọng (trộn về mono) → gate → EQ → WAV stereo (hai kênh như nhau). In một dòng JSON: độ to
//       (LUFS) vào / ra, đỉnh, thời gian xử lý.
//   fxlab response [khối EQ…] [--rate 48000]
//       Bảng đáp ứng biên độ của EQ: đo trên đáp ứng xung so với công thức RBJ cookbook.
//
// Khối FX (thứ tự trong chuỗi: gate → EQ):
//   --gate ngưỡng_dB            --hpf Hz  --lpf Hz
//   --bell f,dB,Q  --lowshelf f,dB,Q  --highshelf f,dB,Q   (tổng cộng tối đa 5 băng)
// File có bản quyền chỉ để trong testdata/ (không commit).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "dsp/biquad.h"
#include "fx/equalizer.h"
#include "fx/noise_gate.h"
#include "key/loudness.h"
#include "util/fx_eval.h"
#include "util/wav_out.h"

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(stderr,
               "Cách dùng:\n"
               "  fxlab render <file> -o out.wav [--start giây] [--dur giây] [khối FX]\n"
               "  fxlab response [khối EQ] [--rate 48000]\n"
               "Khối FX: --gate dB  --hpf Hz  --lpf Hz  --bell f,dB,Q  --lowshelf f,dB,Q\n"
               "         --highshelf f,dB,Q   (tối đa 5 băng)\n");
  return 2;
}

struct Chain {
  bool gate = false;
  GateParams gateParams;
  EqParams eq;
  int bands = 0;
};

// Đọc một khối FX ở argv[i] (và giá trị ở argv[i + 1]). Trả số đối số đã dùng (0 = không phải).
int parseFx(Chain& c, int i, int argc, char** argv) {
  if (i + 1 >= argc) return 0;
  const std::string a = argv[i];
  const char* v = argv[i + 1];
  if (a == "--gate") {
    c.gate = true;
    c.gateParams.thresholdDb = static_cast<float>(std::atof(v));
    return 2;
  }
  if (a == "--hpf") {
    c.eq.highPassHz = static_cast<float>(std::atof(v));
    return 2;
  }
  if (a == "--lpf") {
    c.eq.lowPassHz = static_cast<float>(std::atof(v));
    return 2;
  }
  SvfType type = SvfType::Off;
  if (a == "--bell") type = SvfType::Bell;
  if (a == "--lowshelf") type = SvfType::LowShelf;
  if (a == "--highshelf") type = SvfType::HighShelf;
  if (type == SvfType::Off) return 0;
  float f = 0.0f, db = 0.0f, q = 0.7071f;
  if (std::sscanf(v, "%f,%f,%f", &f, &db, &q) < 2 || c.bands >= EqParams::kBands) return 0;
  c.eq.bands[c.bands++] = {type, f, db, q};
  return 2;
}

BiquadCoeffs rbjOf(SvfType type, double fs, double f, double q, double db) {
  switch (type) {
    case SvfType::Bell:
      return BiquadCoeffs::peaking(fs, f, q, db);
    case SvfType::LowShelf:
      return BiquadCoeffs::lowShelf(fs, f, q, db);
    case SvfType::HighShelf:
      return BiquadCoeffs::highShelf(fs, f, q, db);
    default:
      return BiquadCoeffs{};
  }
}

std::optional<float> lufsOf(const std::vector<float>& mono, uint32_t rate) {
  DecodedAudio a;
  a.sampleRate = rate;
  a.channels = 1;
  a.frames = mono.size();
  a.samples.resize(mono.size());
  for (std::size_t i = 0; i < mono.size(); ++i) {
    a.samples[i] =
        static_cast<int16_t>(std::lrint(std::fmax(-1.0f, std::fmin(1.0f, mono[i])) * 32767.0f));
  }
  return measureLoudness(a).integratedLufs;
}

int cmdRender(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string in = argv[2];
  std::string out;
  double startSec = 0.0, durSec = 0.0;
  Chain chain;
  for (int i = 3; i < argc;) {
    const std::string a = argv[i];
    const bool hasVal = i + 1 < argc;
    if (a == "-o" && hasVal) {
      out = argv[i + 1];
      i += 2;
    } else if (a == "--start" && hasVal) {
      startSec = std::atof(argv[i + 1]);
      i += 2;
    } else if (a == "--dur" && hasVal) {
      durSec = std::atof(argv[i + 1]);
      i += 2;
    } else if (const int used = parseFx(chain, i, argc, argv)) {
      i += used;
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
  const auto first =
      std::min<uint64_t>(dec.frames, static_cast<uint64_t>(startSec * dec.sampleRate));
  uint64_t frames = dec.frames - first;
  if (durSec > 0.0) {
    frames = std::min<uint64_t>(frames, static_cast<uint64_t>(durSec * dec.sampleRate));
  }
  // Giọng là mono: trộn các kênh.
  std::vector<float> x(static_cast<std::size_t>(frames));
  const std::size_t ch = dec.channels;
  for (std::size_t i = 0; i < x.size(); ++i) {
    float sum = 0.0f;
    for (std::size_t c = 0; c < ch; ++c) {
      sum += static_cast<float>(dec.samples[(first + i) * ch + c]) / 32768.0f;
    }
    x[i] = sum / static_cast<float>(ch);
  }
  const auto lufsIn = lufsOf(x, dec.sampleRate);

  const auto t0 = std::chrono::steady_clock::now();
  NoiseGate gate(dec.sampleRate);
  gate.setParams(chain.gateParams);
  Equalizer eq(dec.sampleRate);
  eq.setParamsImmediate(chain.eq);
  constexpr uint32_t kBlock = 256;
  for (std::size_t i = 0; i < x.size(); i += kBlock) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(kBlock, x.size() - i));
    if (chain.gate) gate.process(x.data() + i, n);
    eq.process(x.data() + i, n);
  }
  const auto t1 = std::chrono::steady_clock::now();

  float peak = 0.0f;
  std::vector<float> stereo(2 * x.size());
  for (std::size_t i = 0; i < x.size(); ++i) {
    peak = std::fmax(peak, std::fabs(x[i]));
    stereo[2 * i] = x[i];
    stereo[2 * i + 1] = x[i];
  }
  if (!test::writeWav(out, stereo, 2, dec.sampleRate)) {
    std::fprintf(stderr, "Không ghi được %s\n", out.c_str());
    return 1;
  }
  const auto lufsOut = lufsOf(x, dec.sampleRate);
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  std::printf(
      "{\"file\": \"%s\", \"out\": \"%s\", \"duration_sec\": %.3f, \"lufs_in\": %.2f, "
      "\"lufs_out\": %.2f, \"peak_out_db\": %.2f, \"process_sec\": %.4f, \"realtime_x\": %.0f}\n",
      in.c_str(), out.c_str(), static_cast<double>(frames) / dec.sampleRate,
      static_cast<double>(lufsIn.value_or(-99.0f)), static_cast<double>(lufsOut.value_or(-99.0f)),
      20.0 * std::log10(static_cast<double>(peak) + 1e-12), sec,
      sec > 0.0 ? static_cast<double>(frames) / dec.sampleRate / sec : 0.0);
  return 0;
}

int cmdResponse(int argc, char** argv) {
  Chain chain;
  double rate = 48000.0;
  for (int i = 2; i < argc;) {
    const std::string a = argv[i];
    if (a == "--rate" && i + 1 < argc) {
      rate = std::atof(argv[i + 1]);
      i += 2;
    } else if (const int used = parseFx(chain, i, argc, argv)) {
      i += used;
    } else {
      return usage();
    }
  }
  Equalizer eq(rate);
  eq.setParamsImmediate(chain.eq);
  const auto ir = test::impulseResponse([&](float* x, uint32_t n) { eq.process(x, n); },
                                        static_cast<std::size_t>(rate));
  std::printf("| Tần số (Hz) | EQ đo (dB) | RBJ (dB) | Lệch (dB) |\n|---|---|---|---|\n");
  double worst = 0.0;
  for (const double f : test::logFrequencies(32)) {
    double ref = 1.0;
    if (chain.eq.highPassHz > 0.0f) {
      ref *= BiquadCoeffs::highpass(rate, static_cast<double>(chain.eq.highPassHz))
                 .magnitudeAt(f, rate);
    }
    if (chain.eq.lowPassHz > 0.0f) {
      ref *=
          BiquadCoeffs::lowpass(rate, static_cast<double>(chain.eq.lowPassHz)).magnitudeAt(f, rate);
    }
    for (int b = 0; b < chain.bands; ++b) {
      const EqBand& band = chain.eq.bands[b];
      ref *= rbjOf(band.type, rate, static_cast<double>(band.freqHz), static_cast<double>(band.q),
                   static_cast<double>(band.gainDb))
                 .magnitudeAt(f, rate);
    }
    const double measured = test::magnitudeDb(ir, f, rate);
    const double want = 20.0 * std::log10(ref);
    worst = std::fmax(worst, std::fabs(measured - want));
    std::printf("| %.0f | %+.2f | %+.2f | %+.3f |\n", f, measured, want, measured - want);
  }
  std::printf("\nLệch lớn nhất: %.3f dB\n", worst);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  if (std::strcmp(argv[1], "render") == 0) return cmdRender(argc, argv);
  if (std::strcmp(argv[1], "response") == 0) return cmdResponse(argc, argv);
  return usage();
}
