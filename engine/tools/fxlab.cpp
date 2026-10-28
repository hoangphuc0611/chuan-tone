// fxlab — thử bộ FX giọng (M5) trên máy dev.
//
//   fxlab render <file> -o out.wav [--start giây] [--dur giây] [khối FX…]
//       Giọng (trộn về mono) → gate → EQ → WAV stereo (hai kênh như nhau). In một dòng JSON: độ to
//       (LUFS) vào / ra, đỉnh, thời gian xử lý.
//   fxlab response [khối EQ…] [--rate 48000]
//       Bảng đáp ứng biên độ của EQ: đo trên đáp ứng xung so với công thức RBJ cookbook.
//   fxlab curve --comp ngưỡng,tỉ_lệ[,knee,attack,release,makeup]
//       Đường nén tĩnh đo bằng sin 1 kHz so với công thức knee.
//   fxlab ir --reverb room|hall|plate,RT60[,pre_delay_ms,ướt,damping]
//       RT60 theo băng, tương quan L–R, độ nhô phổ của đáp ứng xung reverb.
//
// Khối FX (thứ tự trong chuỗi: gate → EQ → compressor → de-esser → delay → reverb; ra stereo =
// khô + vọng + vang, tiếng vọng cũng vào reverb):
//   --gate ngưỡng_dB            --hpf Hz  --lpf Hz
//   --bell f,dB,Q  --lowshelf f,dB,Q  --highshelf f,dB,Q   (tổng cộng tối đa 5 băng)
//   --comp ngưỡng,tỉ_lệ[,knee,attack_ms,release_ms,makeup]   --deess ngưỡng[,giảm_tối_đa]
//   --delay 1/4|1/8|1/8d|1/16|ms,hồi_tiếp,ướt[,lpf_Hz]  --bpm 92
//   --reverb room|hall|plate,RT60[,pre_delay_ms,ướt,damping]
// File có bản quyền chỉ để trong testdata/ (không commit).

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "dsp/biquad.h"
#include "fx/compressor.h"
#include "fx/de_esser.h"
#include "fx/delay.h"
#include "fx/equalizer.h"
#include "fx/noise_gate.h"
#include "fx/reverb.h"
#include "key/loudness.h"
#include "util/fx_eval.h"
#include "util/wav_out.h"

using namespace tonecore;

namespace {

int usage() {
  std::fprintf(
      stderr,
      "Cách dùng:\n"
      "  fxlab render <file> -o out.wav [--start giây] [--dur giây] [khối FX]\n"
      "  fxlab response [khối EQ] [--rate 48000]\n"
      "  fxlab curve --comp ngưỡng,tỉ_lệ[,knee,attack,release,makeup]\n"
      "Khối FX: --gate dB  --hpf Hz  --lpf Hz  --bell f,dB,Q  --lowshelf f,dB,Q\n"
      "         --highshelf f,dB,Q   (tối đa 5 băng)\n"
      "         --comp ngưỡng,tỉ_lệ[,knee,attack,release,makeup]  --deess ngưỡng[,giảm]\n");
  return 2;
}

struct Chain {
  bool gate = false;
  GateParams gateParams;
  EqParams eq;
  int bands = 0;
  bool comp = false;
  CompressorParams compParams;
  bool deess = false;
  DeEsserParams deessParams;
  bool delay = false;
  DelayParams delayParams;
  bool reverb = false;
  ReverbParams reverbParams;
};

// "1/4", "1/8", "1/8d", "1/16" hoặc số mili giây.
bool parseDivision(const std::string& s, DelayParams& p) {
  if (s == "1/4") {
    p.division = NoteDivision::Quarter;
  } else if (s == "1/8") {
    p.division = NoteDivision::Eighth;
  } else if (s == "1/8d") {
    p.division = NoteDivision::DottedEighth;
  } else if (s == "1/16") {
    p.division = NoteDivision::Sixteenth;
  } else if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0]))) {
    p.division = NoteDivision::Milliseconds;
    p.timeMs = static_cast<float>(std::atof(s.c_str()));
  } else {
    return false;
  }
  return true;
}

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
  if (a == "--comp") {  // ngưỡng,tỉ lệ[,knee,attack,release,makeup]
    c.comp = true;
    CompressorParams& p = c.compParams;
    if (std::sscanf(v, "%f,%f,%f,%f,%f,%f", &p.thresholdDb, &p.ratio, &p.kneeDb, &p.attackMs,
                    &p.releaseMs, &p.makeupDb) < 2) {
      return 0;
    }
    return 2;
  }
  if (a == "--deess") {  // ngưỡng[,giảm tối đa]
    c.deess = true;
    if (std::sscanf(v, "%f,%f", &c.deessParams.thresholdDb, &c.deessParams.maxReductionDb) < 1) {
      return 0;
    }
    return 2;
  }
  if (a == "--delay") {  // phần_nốt|ms,hồi_tiếp,ướt[,lpf]
    char div[16] = {};
    float fb = 0.25f, mix = 0.2f, lpf = 4000.0f;
    if (std::sscanf(v, "%15[^,],%f,%f,%f", div, &fb, &mix, &lpf) < 3 ||
        !parseDivision(div, c.delayParams)) {
      return 0;
    }
    c.delay = true;
    c.delayParams.feedback = fb;
    c.delayParams.mix = mix;
    c.delayParams.lowPassHz = lpf;
    return 2;
  }
  if (a == "--bpm") {
    c.delayParams.bpm = static_cast<float>(std::atof(v));
    return 2;
  }
  if (a == "--reverb") {  // room|hall|plate,RT60[,pre_delay_ms,ướt,damping]
    char type[16] = {};
    ReverbParams& p = c.reverbParams;
    if (std::sscanf(v, "%15[^,],%f,%f,%f,%f", type, &p.decaySec, &p.preDelayMs, &p.mix,
                    &p.damping) < 2) {
      return 0;
    }
    const std::string t = type;
    p.type = t == "room" ? ReverbType::Room : t == "plate" ? ReverbType::Plate : ReverbType::Hall;
    c.reverb = true;
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
  Compressor comp(dec.sampleRate);
  comp.setParams(chain.compParams);
  comp.reset();
  DeEsser deess(dec.sampleRate);
  deess.setParams(chain.deessParams);
  Delay delay(dec.sampleRate);
  delay.setParams(chain.delayParams);
  delay.reset();
  Reverb reverb(dec.sampleRate);
  reverb.setParams(chain.reverbParams);
  reverb.reset();
  std::vector<float> outL(x.size(), 0.0f), outR(x.size(), 0.0f);
  std::vector<float> echo(256), send(256), revL(256), revR(256);
  float maxComp = 0.0f, maxDeess = 0.0f;
  constexpr uint32_t kBlock = 256;
  for (std::size_t i = 0; i < x.size(); i += kBlock) {
    const auto n = static_cast<uint32_t>(std::min<std::size_t>(kBlock, x.size() - i));
    if (chain.gate) gate.process(x.data() + i, n);
    eq.process(x.data() + i, n);
    if (chain.comp) {
      comp.process(x.data() + i, n);
      maxComp = std::fmin(maxComp, comp.gainReductionDb());
    }
    if (chain.deess) {
      deess.process(x.data() + i, n);
      maxDeess = std::fmin(maxDeess, deess.gainReductionDb());
    }
    // Khô + vọng (giữa) + vang (stereo); tiếng vọng cũng vào reverb.
    std::fill(echo.begin(), echo.end(), 0.0f);
    if (chain.delay) delay.process(x.data() + i, echo.data(), n);
    for (uint32_t k = 0; k < n; ++k) send[k] = x[i + k] + echo[k];
    std::fill(revL.begin(), revL.end(), 0.0f);
    std::fill(revR.begin(), revR.end(), 0.0f);
    if (chain.reverb) reverb.process(send.data(), revL.data(), revR.data(), n);
    for (uint32_t k = 0; k < n; ++k) {
      outL[i + k] = send[k] + revL[k];
      outR[i + k] = send[k] + revR[k];
    }
  }
  const auto t1 = std::chrono::steady_clock::now();

  float peak = 0.0f;
  std::vector<float> stereo(2 * x.size());
  std::vector<float> mid(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) {
    peak = std::fmax(peak, std::fmax(std::fabs(outL[i]), std::fabs(outR[i])));
    stereo[2 * i] = outL[i];
    stereo[2 * i + 1] = outR[i];
    mid[i] = 0.5f * (outL[i] + outR[i]);
  }
  if (!test::writeWav(out, stereo, 2, dec.sampleRate)) {
    std::fprintf(stderr, "Không ghi được %s\n", out.c_str());
    return 1;
  }
  const auto lufsOut = lufsOf(mid, dec.sampleRate);
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  std::printf(
      "{\"file\": \"%s\", \"out\": \"%s\", \"duration_sec\": %.3f, \"lufs_in\": %.2f, "
      "\"lufs_out\": %.2f, \"peak_out_db\": %.2f, \"comp_max_db\": %.1f, \"deess_max_db\": %.1f, "
      "\"process_sec\": %.4f, \"realtime_x\": %.0f}\n",
      in.c_str(), out.c_str(), static_cast<double>(frames) / dec.sampleRate,
      static_cast<double>(lufsIn.value_or(-99.0f)), static_cast<double>(lufsOut.value_or(-99.0f)),
      20.0 * std::log10(static_cast<double>(peak) + 1e-12), static_cast<double>(maxComp),
      static_cast<double>(maxDeess), sec,
      sec > 0.0 ? static_cast<double>(frames) / dec.sampleRate / sec : 0.0);
  return 0;
}

// Đường nén tĩnh của compressor: đo bằng sin 1 kHz so với công thức knee.
int cmdCurve(int argc, char** argv) {
  Chain chain;
  for (int i = 2; i < argc;) {
    if (const int used = parseFx(chain, i, argc, argv)) {
      i += used;
    } else {
      return usage();
    }
  }
  if (!chain.comp) return usage();
  constexpr double kRate = 48000.0;
  Compressor comp(kRate);
  comp.setParams(chain.compParams);
  comp.reset();
  std::vector<double> levels;
  for (double l = -60.0; l <= 0.0; l += 3.0) levels.push_back(l);
  const auto out = test::staticCurveDb([&](float* x, uint32_t n) { comp.process(x, n); }, levels,
                                       1000.0, kRate, 0.6);
  std::printf("| Vào (dB) | Ra đo (dB) | Công thức (dB) | Lệch (dB) |\n|---|---|---|---|\n");
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const double want = levels[i] + Compressor::staticGainDb(levels[i], chain.compParams) +
                        static_cast<double>(chain.compParams.makeupDb);
    std::printf("| %.0f | %+.2f | %+.2f | %+.3f |\n", levels[i], out[i], want, out[i] - want);
  }
  return 0;
}

// Đáp ứng xung của reverb: RT60 theo băng (T30), tương quan L–R của đuôi, độ nhô phổ, mức vang
// trong băng giọng — để chỉnh preset bằng số trước khi nghe.
int cmdIr(int argc, char** argv) {
  Chain chain;
  for (int i = 2; i < argc;) {
    if (const int used = parseFx(chain, i, argc, argv)) {
      i += used;
    } else {
      return usage();
    }
  }
  if (!chain.reverb) return usage();
  constexpr double kRate = 48000.0;
  ReverbParams p = chain.reverbParams;
  p.mix = 1.0f;
  Reverb rv(kRate);
  rv.setParams(p);
  rv.reset();
  const auto n = static_cast<std::size_t>((1.5 * static_cast<double>(p.decaySec) + 0.4) * kRate);
  std::vector<float> in(n, 0.0f), l(n), r(n);
  in[0] = 1.0f;
  rv.process(in.data(), l.data(), r.data(), static_cast<uint32_t>(n));
  std::printf("| Băng | RT60 L (s) | RT60 R (s) |\n|---|---|---|\n");
  const double bands[][2] = {{125, 250},   {250, 500},   {500, 1000},
                             {1000, 2000}, {2000, 4000}, {4000, 8000}};
  for (const auto& b : bands) {
    std::printf("| %.0f–%.0f Hz | %.2f | %.2f |\n", b[0], b[1], test::rt60(l, kRate, b[0], b[1]),
                test::rt60(r, kRate, b[0], b[1]));
  }
  const auto from = static_cast<std::size_t>(0.05 * kRate);
  std::printf(
      "\nĐặt RT60 %.2f s (4 kHz: %.2f s) · tương quan L–R đuôi %.3f · nhô phổ (200 Hz–8 kHz) "
      "L %.1f / R %.1f dB · dòng FDN (mẫu):",
      static_cast<double>(p.decaySec),
      static_cast<double>(p.decaySec) * (1.0 - static_cast<double>(p.damping)),
      test::correlation(l, r, from, n),
      test::spectralPeakinessDb(l, kRate, static_cast<std::size_t>(0.1 * kRate), n, 200.0, 8000.0),
      test::spectralPeakinessDb(r, kRate, static_cast<std::size_t>(0.1 * kRate), n, 200.0, 8000.0));
  for (const auto m : rv.lineLengths()) std::printf(" %u", m);
  std::printf("\n");
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
  if (std::strcmp(argv[1], "curve") == 0) return cmdCurve(argc, argv);
  if (std::strcmp(argv[1], "ir") == 0) return cmdIr(argc, argv);
  return usage();
}
