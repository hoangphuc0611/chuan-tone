#include "util/stretch_eval.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "decode/decoder.h"
#include "dsp/fft.h"
#include "key/loudness.h"
#include "util/synth_music.h"

namespace tonecore::test {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> monoOf(const std::vector<float>& stereo) {
  std::vector<float> m(stereo.size() / 2);
  for (std::size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
  return m;
}

std::vector<float> toStereo(const std::vector<float>& left, const std::vector<float>& right) {
  std::vector<float> s(2 * left.size());
  for (std::size_t i = 0; i < left.size(); ++i) {
    s[2 * i] = left[i];
    s[2 * i + 1] = right[i];
  }
  return s;
}

// Phổ công suất đoạn [start, end) với cửa sổ Blackman–Harris 4 số hạng (búp phụ −92 dB), đệm 0 tới
// luỹ thừa 2 ≥ 2× độ dài. Trả về công suất từng bin; `binHz` = độ rộng một bin.
std::vector<double> powerSpectrum(const std::vector<float>& x, double rate, double startSec,
                                  double endSec, double& binHz) {
  const auto a = static_cast<std::size_t>(std::max(0.0, startSec * rate));
  const auto b = std::min(x.size(), static_cast<std::size_t>(endSec * rate));
  const std::size_t len = b > a ? b - a : 0;
  std::size_t n = 4;
  while (n < 2 * len) n <<= 1;
  std::vector<float> buf(n, 0.0f);
  for (std::size_t i = 0; i < len; ++i) {
    const double u = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(len - 1);
    const double w =
        0.35875 - 0.48829 * std::cos(u) + 0.14128 * std::cos(2 * u) - 0.01168 * std::cos(3 * u);
    buf[i] = static_cast<float>(w * x[a + i]);
  }
  RealFft fft(n);
  std::vector<std::complex<float>> spec(fft.bins());
  fft.forward(buf.data(), spec.data());
  std::vector<double> p(spec.size());
  for (std::size_t k = 0; k < spec.size(); ++k) p[k] = std::norm(std::complex<double>(spec[k]));
  binHz = rate / static_cast<double>(n);
  return p;
}

// Tần số đỉnh trong ±rangeCents quanh `guessHz`, nội suy parabol trên log biên độ.
double peakFrequency(const std::vector<double>& p, double binHz, double guessHz,
                     double rangeCents) {
  const double r = std::pow(2.0, rangeCents / 1200.0);
  const auto lo = static_cast<std::size_t>(std::max(1.0, guessHz / r / binHz));
  const auto hi = std::min(p.size() - 2, static_cast<std::size_t>(guessHz * r / binHz) + 1);
  std::size_t best = lo;
  for (std::size_t k = lo; k <= hi; ++k) {
    if (p[k] > p[best]) best = k;
  }
  const double al = std::log(p[best - 1] + 1e-30);
  const double be = std::log(p[best] + 1e-30);
  const double ga = std::log(p[best + 1] + 1e-30);
  const double den = al - 2 * be + ga;
  const double delta = den != 0.0 ? 0.5 * (al - ga) / den : 0.0;
  return (static_cast<double>(best) + delta) * binHz;
}

// Năng lượng ngoài các hoạ âm (±max(8 bin, 10 cent)) so với tổng, bỏ dưới 20 Hz.
double offPartialDb(const std::vector<double>& p, double binHz,
                    const std::vector<double>& partials) {
  std::vector<bool> in(p.size(), false);
  for (const double f : partials) {
    const double tol = std::max(8.0 * binHz, f * (std::pow(2.0, 10.0 / 1200.0) - 1.0));
    const auto lo = static_cast<std::size_t>(std::max(0.0, (f - tol) / binHz));
    const auto hi = std::min(p.size() - 1, static_cast<std::size_t>((f + tol) / binHz) + 1);
    for (std::size_t k = lo; k <= hi; ++k) in[k] = true;
  }
  double total = 0.0;
  double outside = 0.0;
  for (auto k = static_cast<std::size_t>(20.0 / binHz) + 1; k < p.size(); ++k) {
    total += p[k];
    if (!in[k]) outside += p[k];
  }
  return 10.0 * std::log10(std::max(outside, 1e-30) / std::max(total, 1e-30));
}

// Bao biên RMS cửa sổ `windowSec` (căn giữa).
std::vector<double> envelope(const std::vector<float>& x, double rate, double windowSec = 0.001) {
  std::vector<double> prefix(x.size() + 1, 0.0);
  for (std::size_t i = 0; i < x.size(); ++i) {
    prefix[i + 1] = prefix[i] + static_cast<double>(x[i]) * x[i];
  }
  const auto half = static_cast<std::size_t>(0.5 * windowSec * rate);
  std::vector<double> e(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) {
    const std::size_t a = i > half ? i - half : 0;
    const std::size_t b = std::min(x.size(), i + half + 1);
    e[i] = std::sqrt((prefix[b] - prefix[a]) / static_cast<double>(b - a));
  }
  return e;
}

struct HitStats {
  double preEchoDb;
  double riseMs;
  double reboundDb;
  double startMs;  // thời điểm bao biên vượt 10% đỉnh, so với onset đáp án
};

// `env`: bao biên 1 ms (đỉnh, thời gian lên, thời điểm bắt đầu); `env5`: bao biên 5 ms của sai phân
// bậc 1 (dội sau đỉnh — nhấn cao tần, nơi trống "đánh đôi" nghe rõ; thân kick trầm gợn theo từng
// nửa chu kỳ nên không tính; cửa sổ 5 ms mịn hơn để dao động tự nhiên của nhiễu snare/hi-hat không
// bị tính là dội).
HitStats hitStats(const std::vector<float>& x, const std::vector<double>& env,
                  const std::vector<double>& env5, double rate, std::size_t t0) {
  const auto ms = [rate](double v) { return static_cast<std::size_t>(v * 0.001 * rate); };
  std::size_t peakIdx = t0 - ms(2);
  for (std::size_t i = t0 - ms(2); i < t0 + ms(30); ++i) {
    if (env[i] > env[peakIdx]) peakIdx = i;
  }
  const double peak = std::max(env[peakIdx], 1e-12);
  double pre = 0.0;
  for (std::size_t i = t0 - ms(50); i < t0 - ms(5); ++i) pre += static_cast<double>(x[i]) * x[i];
  pre /= static_cast<double>(ms(45));
  std::size_t i10 = peakIdx;
  std::size_t i90 = peakIdx;
  for (std::size_t i = t0 - ms(20); i <= peakIdx; ++i) {
    if (env[i] >= 0.1 * peak) {
      i10 = i;
      break;
    }
  }
  for (std::size_t i = i10; i <= peakIdx; ++i) {
    if (env[i] >= 0.9 * peak) {
      i90 = i;
      break;
    }
  }
  // Dội lại (trống "đánh đôi"): sau đỉnh 5 ms, bao biên tăng trở lại bao nhiêu so với mức thấp
  // nhất đã qua — tiếng trống gốc chỉ tắt dần nên gần như không có.
  std::size_t peak5 = t0;
  for (std::size_t i = t0; i < t0 + ms(30); ++i) {
    if (env5[i] > env5[peak5]) peak5 = i;
  }
  double runMin = env5[peak5];
  double rebound = 0.0;
  for (std::size_t i = peak5 + ms(5); i < peak5 + ms(80) && i < env5.size(); ++i) {
    runMin = std::min(runMin, env5[i]);
    rebound = std::max(rebound, env5[i] - runMin);
  }
  return {10.0 * std::log10(std::max(pre, 1e-30) / (peak * peak)),
          1000.0 * static_cast<double>(i90 - i10) / rate,
          20.0 * std::log10(std::max(rebound, 1e-12) / std::max(env5[peak5], 1e-12)),
          1000.0 * (static_cast<double>(i10) - static_cast<double>(t0)) / rate};
}

std::vector<float> firstDifference(const std::vector<float>& x) {
  std::vector<float> d(x.size());
  for (std::size_t i = 1; i < x.size(); ++i) d[i] = x[i] - x[i - 1];
  return d;
}

double median(std::vector<double> v) {
  if (v.empty()) return kNaN;
  std::sort(v.begin(), v.end());
  const std::size_t m = v.size() / 2;
  return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

double correlation(const std::vector<float>& stereo, double rate) {
  const auto skip = static_cast<std::size_t>(0.5 * rate);
  const std::size_t frames = stereo.size() / 2;
  double lr = 0.0;
  double ll = 0.0;
  double rr = 0.0;
  for (std::size_t i = skip; i + skip < frames; ++i) {
    const double l = stereo[2 * i];
    const double r = stereo[2 * i + 1];
    lr += l * r;
    ll += l * l;
    rr += r * r;
  }
  return lr / std::sqrt(std::max(ll * rr, 1e-30));
}

double lufs(const std::vector<float>& stereo, double rate) {
  DecodedAudio a;
  a.sampleRate = static_cast<uint32_t>(rate);
  a.channels = 2;
  a.frames = stereo.size() / 2;
  a.samples.resize(stereo.size());
  for (std::size_t i = 0; i < stereo.size(); ++i) {
    a.samples[i] = static_cast<int16_t>(std::lround(std::clamp(stereo[i], -1.0f, 1.0f) * 32767.0f));
  }
  const LoudnessResult r = measureLoudness(a);
  return r.integratedLufs ? static_cast<double>(*r.integratedLufs) : kNaN;
}

StretchMetrics measure(const StretchSignal& sig, const std::vector<float>& out, double semitones,
                       bool isReference) {
  StretchMetrics m;
  const double beta = std::pow(2.0, semitones / 12.0);
  const std::vector<float> mono = monoOf(out);
  if (!sig.partialsHz.empty()) {
    double binHz = 0.0;
    const auto p = powerSpectrum(mono, sig.rate, sig.steadyStart, sig.steadyEnd, binHz);
    std::vector<double> shifted;
    for (const double f : sig.partialsHz) {
      if (f * beta < 0.45 * sig.rate) shifted.push_back(f * beta);
    }
    m.offPartialDb = offPartialDb(p, binHz, shifted);
    if (sig.toneHz > 0.0) {
      const double want = sig.toneHz * beta;
      const double got = peakFrequency(p, binHz, want, 100.0);
      m.pitchErrorCents = std::abs(1200.0 * std::log2(got / want));
    }
  }
  if (!sig.onsets.empty()) {
    const std::vector<float> refMono = monoOf(sig.stereo);
    const auto envOut = envelope(mono, sig.rate);
    const auto envRef = envelope(refMono, sig.rate);
    const auto env5Out = envelope(firstDifference(mono), sig.rate, 0.005);
    const auto env5Ref = envelope(firstDifference(refMono), sig.rate, 0.005);
    std::vector<double> pre;
    std::vector<double> rise;
    double worstPre = -1e9;
    double worstRebound = -1e9;
    double maxLag = 0.0;
    for (const double t : sig.onsets) {
      const auto t0 = static_cast<std::size_t>(std::llround(t * sig.rate));
      const HitStats o = hitStats(mono, envOut, env5Out, sig.rate, t0);
      const HitStats r = hitStats(refMono, envRef, env5Ref, sig.rate, t0);
      pre.push_back(o.preEchoDb);
      rise.push_back(isReference ? o.riseMs : o.riseMs - r.riseMs);
      worstPre = std::max(worstPre, o.preEchoDb);
      worstRebound = std::max(worstRebound, o.reboundDb);
      maxLag = std::max(maxLag, std::abs(o.startMs - r.startMs));
    }
    m.preEchoDb = median(pre);
    m.preEchoWorstDb = worstPre;
    m.riseDeltaMs = median(rise);
    m.reboundDb = worstRebound;
    m.lagMaxMs = maxLag;
  }
  if (sig.measureNoise) {
    const auto skip = static_cast<std::size_t>(0.5 * sig.rate);
    double pin = 0.0;
    double pout = 0.0;
    for (std::size_t i = skip; i + skip < mono.size(); ++i) {
      const auto& ref = sig.stereo;
      const double r = 0.5 * (static_cast<double>(ref[2 * i]) + ref[2 * i + 1]);
      pin += r * r;
      pout += static_cast<double>(mono[i]) * mono[i];
    }
    const double ideal = isReference ? 1.0 : 0.97 * std::min(1.0, 1.0 / beta);
    m.noiseLevelDb = 10.0 * std::log10(pout / pin / ideal);
  }
  if (sig.measureStereo) {
    m.correlationDelta =
        isReference ? correlation(out, sig.rate)
                    : std::abs(correlation(out, sig.rate) - correlation(sig.stereo, sig.rate));
    m.lufsDelta =
        isReference ? lufs(out, sig.rate) : lufs(out, sig.rate) - lufs(sig.stereo, sig.rate);
  }
  return m;
}

}  // namespace

std::vector<StretchSignal> stretchSuite(double rate) {
  std::vector<StretchSignal> suite;
  const auto frames = [rate](double sec) { return static_cast<std::size_t>(sec * rate); };
  NoteSpec flat;
  flat.harmonics = 8;

  {
    StretchSignal s;
    s.name = "sine";
    s.rate = rate;
    std::vector<float> x(frames(3.0));
    for (std::size_t i = 0; i < x.size(); ++i) {
      x[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / rate));
    }
    s.stereo = toStereo(x, x);
    s.toneHz = 440.0;
    s.partialsHz = {440.0};
    s.steadyStart = 0.5;
    s.steadyEnd = 2.5;
    suite.push_back(std::move(s));
  }
  {
    StretchSignal s;
    s.name = "harmonic";
    s.rate = rate;
    std::vector<float> x(frames(3.0));
    addNote(x, rate, 0.0, 3.0, 57.0, 0.3f, flat);
    s.stereo = toStereo(x, x);
    s.toneHz = 220.0;
    for (int h = 1; h <= 8; ++h) s.partialsHz.push_back(220.0 * h);
    s.steadyStart = 0.5;
    s.steadyEnd = 2.5;
    suite.push_back(std::move(s));
  }
  {
    StretchSignal s;
    s.name = "chord";
    s.rate = rate;
    std::vector<float> x(frames(3.0));
    NoteSpec spec;
    spec.harmonics = 6;
    for (const int midi : {48, 60, 64, 67}) {
      addNote(x, rate, 0.0, 3.0, midi, 0.12f, spec);
      for (int h = 1; h <= 6; ++h) s.partialsHz.push_back(midiToHz(midi) * h);
    }
    s.stereo = toStereo(x, x);
    s.steadyStart = 0.5;
    s.steadyEnd = 2.5;
    suite.push_back(std::move(s));
  }
  {
    // Trống đứng riêng: mỗi tiếng cách nhau một móc đơn ở 80 BPM (375 ms) → đuôi tiếng trước đã
    // tắt dưới −60 dB, đo được năng lượng trước onset.
    StretchSignal s;
    s.name = "drums";
    s.rate = rate;
    std::vector<float> x(frames(6.5));
    uint32_t seed = 1;
    for (int step = 0; step < 16; ++step) {
      const double t = 0.5 + 0.375 * step;
      const int pos = step % 8;
      const Drum d =
          pos == 0 || pos == 4 ? Drum::Kick : (pos == 2 || pos == 6 ? Drum::Snare : Drum::HiHat);
      const float amp = d == Drum::Kick ? 0.5f : (d == Drum::Snare ? 0.4f : 0.3f);
      addDrum(x, rate, t, d, amp, seed++);
      s.onsets.push_back(t);
    }
    s.stereo = toStereo(x, x);
    suite.push_back(std::move(s));
  }
  {
    // Beat: trống + vòng hợp âm (pan trái) + bass (giữa) + hi-hat (pan phải).
    StretchSignal s;
    s.name = "beat";
    s.rate = rate;
    const std::size_t n = frames(8.0);
    std::vector<float> center(n);
    std::vector<float> left(n);
    std::vector<float> right(n);
    NoteSpec keys;
    keys.harmonics = 6;
    keys.decayPerSec = 1.5;
    addCadence(left, rate, 0.0, 9, true, 2, 1.0, 7, keys);
    uint32_t seed = 100;
    for (int step = 0; step < 32; ++step) {
      const double t = 0.25 * step;
      const int pos = step % 8;
      if (pos == 0 || pos == 4) addDrum(center, rate, t, Drum::Kick, 0.45f, seed++);
      if (pos == 2 || pos == 6) addDrum(center, rate, t, Drum::Snare, 0.3f, seed++);
      addDrum(right, rate, t, Drum::HiHat, 0.2f, seed++);
    }
    std::vector<float> l(n);
    std::vector<float> r(n);
    for (std::size_t i = 0; i < n; ++i) {
      l[i] = center[i] + 0.8f * left[i] + 0.3f * right[i];
      r[i] = center[i] + 0.3f * left[i] + 0.8f * right[i];
    }
    s.stereo = toStereo(l, r);
    s.measureStereo = true;
    suite.push_back(std::move(s));
  }
  {
    StretchSignal s;
    s.name = "noise";
    s.rate = rate;
    std::vector<float> x(frames(3.0));
    uint32_t state = 12345;
    for (auto& v : x) {
      // LCG đơn giản → nhiễu đều [−0,25, 0,25], cố định giữa các nền tảng.
      state = state * 1664525u + 1013904223u;
      v = 0.5f * (static_cast<float>(state >> 8) / 16777216.0f - 0.5f);
    }
    s.stereo = toStereo(x, x);
    s.measureNoise = true;
    suite.push_back(std::move(s));
  }
  return suite;
}

StretchMetrics measureStretch(const StretchSignal& sig, const std::vector<float>& out,
                              double semitones) {
  return measure(sig, out, semitones, false);
}

StretchMetrics measureReference(const StretchSignal& sig) {
  return measure(sig, sig.stereo, 0.0, true);
}

MelodySignal melodySignal(double seconds, double rate, uint32_t seed) {
  MelodySignal m;
  m.rate = rate;
  const auto frames = static_cast<uint64_t>(seconds * rate);
  const auto noteLen = static_cast<uint64_t>(kMelodyNoteSec * rate);
  m.stereo.resize(2 * frames);
  uint32_t state = seed;
  const auto rnd = [&] {  // LCG: tất định trên mọi nền tảng
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state >> 8) / 16777216.0;
  };
  double fL = 0.0;
  double fR = 0.0;
  double phL = 0.0;
  double phR = 0.0;
  double hit = 0.0;
  const double hitDecay = std::exp(-1.0 / (0.03 * rate));  // tiếng gõ tắt dần τ = 30 ms
  for (uint64_t i = 0; i < frames; ++i) {
    if (i % noteLen == 0) {
      fL = 220.0 * std::pow(4.0, rnd());
      fR = 220.0 * std::pow(4.0, rnd());
      m.noteL.push_back(fL);
      m.noteR.push_back(fR);
    }
    if (rnd() < 2.5 / rate) hit = 0.25;  // ~2,5 tiếng mỗi giây, không theo lưới
    phL += 2.0 * kPi * fL / rate;
    phR += 2.0 * kPi * fR / rate;
    const double noise = hit * (2.0 * rnd() - 1.0);
    hit *= hitDecay;
    m.stereo[2 * i] = static_cast<float>(0.2 * std::sin(phL) + noise);
    m.stereo[2 * i + 1] = static_cast<float>(0.2 * std::sin(phR) + noise);
  }
  return m;
}

std::vector<int> misplacedNotes(const MelodySignal& sig, const std::vector<float>& out,
                                double semitones, uint64_t outStart) {
  // Tần số trội: 4096 mẫu Hann ở giữa nốt (bỏ 60 ms đầu: chỗ đổi nốt + đuôi crossfade), FFT 16384
  // (đệm 0) + nội suy parabol trên log biên độ → sai số ≪ 1%.
  constexpr std::size_t kWin = 4096;
  constexpr std::size_t kFft = 16384;
  RealFft fft(kFft);
  std::vector<float> buf(kFft);
  std::vector<std::complex<float>> spec(fft.bins());
  const auto noteLen = static_cast<uint64_t>(kMelodyNoteSec * sig.rate);
  const auto skip = static_cast<uint64_t>(0.06 * sig.rate);
  const uint64_t outFrames = out.size() / 2;
  const double ratio = std::pow(2.0, semitones / 12.0);
  std::vector<int> bad;
  for (std::size_t k = 0; k < sig.noteL.size(); ++k) {
    const uint64_t from = k * noteLen + skip;
    if (from < outStart || from + kWin > outStart + outFrames || from + kWin > (k + 1) * noteLen) {
      continue;
    }
    bool ok = true;
    for (std::size_t ch = 0; ch < 2 && ok; ++ch) {
      std::fill(buf.begin(), buf.end(), 0.0f);
      for (std::size_t i = 0; i < kWin; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) + 0.5) / kWin);
        buf[i] = static_cast<float>(w) * out[2 * (from - outStart + i) + ch];
      }
      fft.forward(buf.data(), spec.data());
      std::size_t peak = 1;
      for (std::size_t b = 2; b + 1 < spec.size(); ++b) {
        if (std::norm(spec[b]) > std::norm(spec[peak])) peak = b;
      }
      const double a = std::log(std::abs(spec[peak - 1]) + 1e-12);
      const double b = std::log(std::abs(spec[peak]) + 1e-12);
      const double c = std::log(std::abs(spec[peak + 1]) + 1e-12);
      const double den = a - 2.0 * b + c;
      const double offset = den != 0.0 ? 0.5 * (a - c) / den : 0.0;
      const double hz = (static_cast<double>(peak) + offset) * sig.rate / kFft;
      const double want = (ch == 0 ? sig.noteL[k] : sig.noteR[k]) * ratio;
      ok = std::abs(hz / want - 1.0) <= 0.015;
    }
    if (!ok) bad.push_back(static_cast<int>(k));
  }
  return bad;
}

}  // namespace tonecore::test
