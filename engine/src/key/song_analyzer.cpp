#include "key/song_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

#include "dsp/decimator.h"
#include "key/tuning.h"

namespace tonecore {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Khoảng tiến độ tổng của từng giai đoạn, theo thời gian đo trên bài thật (Mac M1, B2). Chỉnh
// lại khi thêm các bước B3–B6 (mục tiêu: thanh tiến độ chạy đều).
struct ProgressSpan {
  float begin;
  float end;
  float at(float fraction) const noexcept { return begin + (end - begin) * fraction; }
};
constexpr ProgressSpan kDecodeSpan{0.0f, 0.10f};
constexpr ProgressSpan kResampleSpan{0.10f, 0.15f};
constexpr ProgressSpan kSeparateSpan{0.15f, 0.70f};
constexpr ProgressSpan kSpectrumSpan{0.70f, 1.0f};

ProgressFn stageProgress(AnalysisControl& ctl, AnalysisStage stage, ProgressSpan span) {
  ctl.report(stage, span.begin);
  return [&ctl, stage, span](float p) {
    ctl.report(stage, span.at(p));
    return !ctl.cancelled();
  };
}

// Trộn các kênh về mono ngay trong buffer interleaved: mẫu mono i ghi vào vị trí i ≤ i·ch nên
// không đè lên dữ liệu chưa đọc.
void mixToMonoInPlace(DecodedAudio& audio) {
  const std::size_t ch = audio.channels;
  const auto frames = static_cast<std::size_t>(audio.frames);
  if (ch > 1) {
    int16_t* s = audio.samples.data();
    const float inv = 1.0f / static_cast<float>(ch);
    for (std::size_t i = 0; i < frames; ++i) {
      int32_t sum = 0;
      for (std::size_t c = 0; c < ch; ++c) sum += s[i * ch + c];
      s[i] = static_cast<int16_t>(std::lround(static_cast<float>(sum) * inv));
    }
  }
  audio.samples.resize(frames);  // giữ capacity: không cấp phát lại
  audio.channels = 1;
}

bool validAudio(const DecodedAudio& audio) {
  return audio.frames > 0 && audio.channels > 0 && audio.sampleRate > 0 &&
         audio.samples.size() >= audio.frames * audio.channels;
}

tc_result analyzeImpl(DecodedAudio& audio, const AnalysisConfig& cfg, AnalysisControl& ctl,
                      SongAnalysis& out) {
  if (!validAudio(audio) || !(cfg.targetAnalysisRate > 0.0)) return TC_ERR_INVALID_ARG;
  out.durationSec = static_cast<double>(audio.frames) / audio.sampleRate;
  out.sourceRate = audio.sampleRate;
  out.channels = audio.channels;
  if (ctl.cancelled()) return kResultCancelled;

  // Giai đoạn 2: tín hiệu phân tích mono.
  auto t0 = Clock::now();
  AnalysisSignal signal;
  tc_result r =
      prepareAnalysisSignal(audio, cfg.targetAnalysisRate,
                            stageProgress(ctl, AnalysisStage::Resampling, kResampleSpan), signal);
  out.timings.push_back({AnalysisStage::Resampling, msSince(t0)});
  if (r != TC_OK) return r;
  out.analysisRate = signal.rate;

  // Giai đoạn 3: tách hoà âm / bộ gõ; các khung phổ hoà âm dùng luôn để ước lượng tuning.
  t0 = Clock::now();
  TuningEstimator tuner(signal.rate, cfg.hpss.fftSize);
  HpssOutput parts;
  r = separateHarmonicPercussive(
      signal.samples, signal.rate, cfg.hpss,
      [&tuner](const float* mag, std::size_t bins, double) { tuner.addFrame(mag, bins); },
      stageProgress(ctl, AnalysisStage::Separating, kSeparateSpan), parts);
  out.timings.push_back({AnalysisStage::Separating, msSince(t0)});
  if (r != TC_OK) return r;
  std::vector<float>().swap(signal.samples);
  out.tuningCents = tuner.estimate();

  // Giai đoạn 4: CQT phần hoà âm (lưới bin dời theo tuning) → chroma bass/treble.
  t0 = Clock::now();
  CqtConfig cqtCfg = cfg.cqt;
  cqtCfg.tuningCents = out.tuningCents.value_or(0.0f);
  CqtSpectrogram cqt;
  r = computeCqt(parts.harmonic, signal.rate, cqtCfg,
                 stageProgress(ctl, AnalysisStage::Spectrum, kSpectrumSpan), cqt);
  if (r == TC_ERR_INVALID_ARG) r = TC_ERR_NOT_SUPPORTED;  // tần số lấy mẫu quá thấp
  if (r == TC_OK) out.chroma = computeChroma(cqt, cfg.chroma);
  out.timings.push_back({AnalysisStage::Spectrum, msSince(t0)});
  if (r != TC_OK) return r;

  // Giai đoạn 5: tone toàn bài.
  t0 = Clock::now();
  ctl.report(AnalysisStage::Key, kSpectrumSpan.end);
  if (const auto key = estimateGlobalKey(out.chroma, cfg.key)) {
    out.key = key->key;
    out.keyConfidence = key->confidence;
  }
  out.timings.push_back({AnalysisStage::Key, msSince(t0)});
  if (ctl.cancelled()) return kResultCancelled;

  // B5–B6: timeline chuyển tone (chroma); BPM (parts.percussive); loudness.

  ctl.report(AnalysisStage::Done, 1.0f);
  return TC_OK;
}

}  // namespace

int analysisDecimation(double sourceRate, double targetRate) {
  if (!(sourceRate > 0.0) || !(targetRate > 0.0)) return 1;
  return std::max(1, static_cast<int>(std::lround(sourceRate / targetRate)));
}

tc_result prepareAnalysisSignal(DecodedAudio& audio, double targetRate,
                                const std::function<bool(float)>& onProgress, AnalysisSignal& out) {
  out = AnalysisSignal{};
  if (!validAudio(audio) || !(targetRate > 0.0)) return TC_ERR_INVALID_ARG;
  mixToMonoInPlace(audio);

  const int factor = analysisDecimation(audio.sampleRate, targetRate);
  out.rate = static_cast<double>(audio.sampleRate) / factor;
  const auto frames = static_cast<std::size_t>(audio.frames);
  out.samples.reserve(frames / static_cast<std::size_t>(factor) + 1);

  // Mặc định của Decimator (96 tap, cắt 0,42 × tần số mới, Kaiser β = 6): với M = 4, mọi thành
  // phần sẽ bị gập (alias) xuống dưới 0,42 × tần số mới đều bị chặn ≥ ~60 dB.
  Decimator dec(factor);
  constexpr float kScale = 1.0f / 32768.0f;
  const std::size_t chunk = audio.sampleRate;  // ~1 s
  std::vector<float> in(chunk);
  std::vector<float> tmp(chunk / static_cast<std::size_t>(factor) + 1);
  const int16_t* src = audio.samples.data();
  for (std::size_t begin = 0; begin < frames; begin += chunk) {
    const std::size_t n = std::min(chunk, frames - begin);
    for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(src[begin + i]) * kScale;
    if (factor == 1) {
      out.samples.insert(out.samples.end(), in.begin(),
                         in.begin() + static_cast<std::ptrdiff_t>(n));
    } else {
      const std::size_t produced = dec.process(in.data(), n, tmp.data());
      out.samples.insert(out.samples.end(), tmp.begin(),
                         tmp.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    if (!onProgress(static_cast<float>(begin + n) / static_cast<float>(frames))) {
      out = AnalysisSignal{};
      return kResultCancelled;
    }
  }

  std::vector<int16_t>().swap(audio.samples);  // trả bộ nhớ PCM gốc ngay
  audio.frames = 0;
  return TC_OK;
}

tc_result analyzeDecoded(DecodedAudio&& audio, const AnalysisConfig& cfg, AnalysisControl& ctl,
                         SongAnalysis& out) {
  out = SongAnalysis{};
  DecodedAudio local = std::move(audio);
  return analyzeImpl(local, cfg, ctl, out);
}

tc_result analyzeFile(const std::string& path, const AnalysisConfig& cfg, AnalysisControl& ctl,
                      SongAnalysis& out) {
  out = SongAnalysis{};
  if (ctl.cancelled()) return kResultCancelled;

  // Giai đoạn 1: giải mã (codec HĐH không báo tiến độ → nhảy một lần khi xong).
  ctl.report(AnalysisStage::Decoding, kDecodeSpan.begin);
  const auto t0 = Clock::now();
  DecodedAudio audio;
  const tc_result r = decodeFile(path, audio);
  out.timings.push_back({AnalysisStage::Decoding, msSince(t0)});
  if (r != TC_OK) return r;
  ctl.report(AnalysisStage::Decoding, kDecodeSpan.end);
  return analyzeImpl(audio, cfg, ctl, out);
}

}  // namespace tonecore
