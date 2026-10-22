#include "key/song_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

#include "dsp/decimator.h"

namespace tonecore {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Khoảng tiến độ tổng của từng giai đoạn. Chỉnh lại theo thời gian đo thật khi thêm các bước
// B2–B6 (mục tiêu: thanh tiến độ chạy đều).
struct ProgressSpan {
  float begin;
  float end;
  float at(float fraction) const noexcept { return begin + (end - begin) * fraction; }
};
constexpr ProgressSpan kDecodeSpan{0.0f, 0.3f};
constexpr ProgressSpan kResampleSpan{0.3f, 1.0f};

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
  ctl.report(AnalysisStage::Resampling, kResampleSpan.begin);
  AnalysisSignal signal;
  const tc_result r = prepareAnalysisSignal(
      audio, cfg.targetAnalysisRate,
      [&ctl](float p) {
        ctl.report(AnalysisStage::Resampling, kResampleSpan.at(p));
        return !ctl.cancelled();
      },
      signal);
  out.timings.push_back({AnalysisStage::Resampling, msSince(t0)});
  if (r != TC_OK) return r;
  out.analysisRate = signal.rate;

  // B2–B6: HPSS → CQT/tuning/chroma → tone + timeline; onset → BPM; loudness.

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
