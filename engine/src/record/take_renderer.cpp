#include "record/take_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>

#include "decode/decoder.h"
#include "fx/fx_rack.h"
#include "fx/true_peak.h"
#include "key/loudness.h"
#include "pitch/voice_pitch_node.h"
#include "player/track.h"
#include "record/wav_file.h"

namespace tonecore {

namespace {

constexpr uint32_t kBlock = 512;
constexpr double kWarmupSec = 2.0;
constexpr double kFadeSec = 0.010;
constexpr float kMinGainDb = -30.0f;
constexpr float kMaxGainDb = 24.0f;
// Trần limiter thấp hơn trần đỉnh thật một chút: hệ số giảm cần L mẫu mới đạt 99%.
constexpr float kCeilingMarginDb = 0.2f;

float dbToGain(float db) {
  return std::pow(10.0f, db / 20.0f);
}

float gainToDb(float g) {
  return g > 0.0f ? 20.0f * std::log10(g) : -200.0f;
}

// File đích: WAV (B3b); M4A thêm ở B3c.
class OutputFile {
 public:
  tc_result open(const std::string& path, int32_t format, uint32_t rate) {
    if (format != TC_RENDER_WAV16 && format != TC_RENDER_WAV_FLOAT) return TC_ERR_NOT_SUPPORTED;
    return wav_.open(path, 2, rate,
                     format == TC_RENDER_WAV_FLOAT ? WavFormat::Float32 : WavFormat::Int16);
  }
  bool write(const float* stereo, std::size_t frames) { return wav_.write(stereo, frames); }
  bool finish() { return wav_.finish(); }
  void abort() { wav_.abort(); }

 private:
  WavWriter wav_;
};

}  // namespace

tc_result renderTake(const TakeRenderSettings& s, TakeRenderProgress& prog) {
  // ---- Nạp bản thu ----
  DecodedAudio beat;
  tc_result r = decodeFile(s.beatPath, beat);
  if (r != TC_OK) return r;
  WavData dry;
  r = readWav(s.voicePath, dry);
  if (r != TC_OK) return r;
  const uint32_t rate = dry.sampleRate;
  auto take = prepareTake(std::move(beat), dry, s.voiceOffsetFrames, rate);
  dry = WavData{};
  if (!take) return TC_ERR_NOT_SUPPORTED;
  const uint64_t total = take->frames;
  const uint64_t begin = std::min(s.startFrame, total);
  const uint64_t end = s.endFrame == 0 ? total : std::min(s.endFrame, total);
  if (end <= begin) return TC_ERR_INVALID_ARG;
  const uint64_t outFrames = end - begin;
  prog.sampleRate.store(rate, std::memory_order_relaxed);
  prog.frames.store(outFrames, std::memory_order_relaxed);

  // ---- Chuỗi giọng (như real-time) ----
  VoicePitchNode pitch(rate, kBlock);
  pitch.setEnabled(s.correctionEnabled);
  pitch.applyPreset(s.correction);
  pitch.setTuningCents(s.tuningCents);
  pitch.setKey(s.keyRoot, s.keyScale);
  pitch.setGateEnabled(s.gateEnabled);
  pitch.setGateThresholdDb(s.gateThresholdDb);
  FxRack fx(rate, kBlock);
  fx.setTempo(s.tempoBpm);
  fx.apply(s.fx);
  fx.setEnabled(s.fxEnabled);
  fx.setWet(s.fxWet);
  const auto lead = static_cast<uint64_t>(pitch.algorithmLatency());

  const std::string tmpPath = s.outPath + ".mix.tmp";
  std::FILE* tmp = std::fopen(tmpPath.c_str(), "wb");
  if (tmp == nullptr) return TC_ERR_INVALID_ARG;
  auto removeTmp = [&] {
    if (tmp != nullptr) std::fclose(tmp);
    tmp = nullptr;
    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);
  };

  // ---- Lượt 1: chuỗi giọng + trộn → file tạm, đo độ to ----
  const auto warm = static_cast<uint64_t>(kWarmupSec * rate);
  const uint64_t from = begin > warm ? begin - warm : 0;
  const auto fade = static_cast<uint64_t>(kFadeSec * rate);
  LoudnessMeter mixMeter(2, rate);
  std::vector<float> voice(kBlock);
  std::vector<float> wl(kBlock);
  std::vector<float> wr(kBlock);
  std::vector<float> mix(2 * kBlock);
  constexpr float kInt16ToFloat = 1.0f / 32768.0f;
  std::size_t segment = 0;
  for (uint64_t at = from; at < end; at += kBlock) {
    if (prog.cancelled.load(std::memory_order_relaxed)) {
      removeTmp();
      return TC_ERR_CANCELLED;
    }
    const auto n = static_cast<uint32_t>(std::min<uint64_t>(kBlock, end - at));
    // Key theo timeline: đoạn chứa đầu block.
    if (!s.timeline.empty()) {
      while (segment + 1 < s.timeline.size() &&
             static_cast<double>(at) * 1000.0 / rate >= s.timeline[segment + 1].startMs) {
        ++segment;
      }
      const auto& seg = s.timeline[segment];
      pitch.setKeyOverride(true, seg.root, seg.scale);
    }
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t k = at + i + lead;
      voice[i] = k < total ? take->voice[k] : 0.0f;
    }
    pitch.process(voice.data(), n);
    fx.process(voice.data(), wl.data(), wr.data(), n);
    if (at + n <= begin) continue;  // còn trong đoạn khởi động
    uint32_t outN = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t k = at + i;
      if (k < begin) continue;
      float g = 1.0f;  // fade ở chỗ cắt
      if (k - begin < fade) g = static_cast<float>(k - begin) / static_cast<float>(fade);
      if (end - k <= fade)
        g = std::min(g, static_cast<float>(end - 1 - k) / static_cast<float>(fade));
      const float bl = static_cast<float>(take->pcm[2 * k]) * kInt16ToFloat;
      const float br = static_cast<float>(take->pcm[2 * k + 1]) * kInt16ToFloat;
      mix[2 * outN] = g * (s.beatGain * bl + s.voiceGain * wl[i]);
      mix[2 * outN + 1] = g * (s.beatGain * br + s.voiceGain * wr[i]);
      ++outN;
    }
    mixMeter.process(mix.data(), outN);
    if (std::fwrite(mix.data(), sizeof(float), 2 * outN, tmp) != 2 * outN) {
      removeTmp();
      return TC_ERR_INTERNAL;
    }
    prog.progress.store(0.8f * static_cast<float>(at + n - from) / static_cast<float>(end - from),
                        std::memory_order_relaxed);
  }
  take.reset();
  if (std::fclose(tmp) != 0) {
    tmp = nullptr;
    removeTmp();
    return TC_ERR_INTERNAL;
  }
  tmp = nullptr;

  // ---- Gain chuẩn hoá ----
  const LoudnessResult mixLoud = mixMeter.result();
  prog.mixLufs.store(
      mixLoud.integratedLufs ? *mixLoud.integratedLufs : std::numeric_limits<float>::quiet_NaN(),
      std::memory_order_relaxed);
  float gainDb = 0.0f;
  if (s.normalize && mixLoud.integratedLufs) {
    gainDb = std::clamp(s.targetLufs - *mixLoud.integratedLufs, kMinGainDb, kMaxGainDb);
  }
  prog.gainDb.store(gainDb, std::memory_order_relaxed);
  const float gain = dbToGain(gainDb);

  // ---- Lượt 2: gain → limiter đỉnh thật → file đích ----
  OutputFile outFile;
  r = outFile.open(s.outPath, s.format, rate);
  if (r != TC_OK) {
    removeTmp();
    return r;
  }
  tmp = std::fopen(tmpPath.c_str(), "rb");
  if (tmp == nullptr) {
    outFile.abort();
    removeTmp();
    return TC_ERR_INTERNAL;
  }
  TruePeakLimiter limiter(2, rate, dbToGain(s.truePeakDb - kCeilingMarginDb));
  const auto latency = static_cast<uint64_t>(limiter.latency());
  LoudnessMeter outMeter(2, rate);
  constexpr std::size_t kChunk = 4096;
  std::vector<float> in(2 * kChunk);
  std::vector<float> out(2 * kChunk);
  float peak = 0.0f;
  TruePeakDetector peakMeter(2);
  std::vector<float> peaks(kChunk);
  // Đẩy outFrames frame thật + `latency` frame im lặng qua limiter; frame ra thứ j ứng với frame
  // xuất j − latency.
  const uint64_t pushTotal = outFrames + latency;
  uint64_t pushed = 0;
  bool ok = true;
  while (pushed < pushTotal && ok) {
    if (prog.cancelled.load(std::memory_order_relaxed)) {
      outFile.abort();
      removeTmp();
      return TC_ERR_CANCELLED;
    }
    const auto n = static_cast<std::size_t>(std::min<uint64_t>(kChunk, pushTotal - pushed));
    const auto real = static_cast<std::size_t>(
        pushed < outFrames ? std::min<uint64_t>(n, outFrames - pushed) : 0);
    if (real > 0 && std::fread(in.data(), 2 * sizeof(float), real, tmp) != real) {
      ok = false;
      break;
    }
    for (std::size_t i = 0; i < 2 * real; ++i) in[i] *= gain;
    std::fill(in.begin() + static_cast<std::ptrdiff_t>(2 * real),
              in.begin() + static_cast<std::ptrdiff_t>(2 * n), 0.0f);
    limiter.process(in.data(), out.data(), static_cast<uint32_t>(n));
    // Phần ra thuộc frame xuất [0, outFrames).
    const uint64_t lo = std::max(pushed, latency);
    const uint64_t hi = std::min(pushed + n, pushTotal);
    pushed += n;
    if (hi <= lo) continue;
    const float* o = out.data() + 2 * (lo - (pushed - n));
    const auto m = static_cast<std::size_t>(hi - lo);
    outMeter.process(o, m);
    peakMeter.process(o, peaks.data(), static_cast<uint32_t>(m));
    for (std::size_t i = 0; i < m; ++i) peak = std::max(peak, peaks[i]);
    ok = outFile.write(o, m);
    prog.progress.store(
        0.8f + 0.2f * static_cast<float>(hi - latency) / static_cast<float>(outFrames),
        std::memory_order_relaxed);
  }
  std::fclose(tmp);
  tmp = nullptr;
  removeTmp();
  {
    std::vector<float> zeros(2 * TruePeakDetector::kDelay, 0.0f);
    peakMeter.process(zeros.data(), peaks.data(), TruePeakDetector::kDelay);
    for (int i = 0; i < TruePeakDetector::kDelay; ++i) peak = std::max(peak, peaks[i]);
  }
  if (!ok || !outFile.finish()) {
    outFile.abort();
    return TC_ERR_INTERNAL;
  }
  const LoudnessResult outLoud = outMeter.result();
  prog.outputLufs.store(
      outLoud.integratedLufs ? *outLoud.integratedLufs : std::numeric_limits<float>::quiet_NaN(),
      std::memory_order_relaxed);
  prog.truePeakDb.store(gainToDb(peak), std::memory_order_relaxed);
  prog.progress.store(1.0f, std::memory_order_relaxed);
  return TC_OK;
}

}  // namespace tonecore
