#include "decode/stems.h"

#include <algorithm>

#include "dsp/resampler.h"

namespace tonecore {

tc_result decodeStems(const std::string& mixPath, const std::vector<std::string>& vocalPaths,
                      DecodedAudio& instrumental, DecodedAudio& vocal) {
  instrumental = DecodedAudio{};
  vocal = DecodedAudio{};
  if (vocalPaths.empty()) return TC_ERR_INVALID_ARG;
  DecodedAudio mix;
  tc_result r = decodeFile(mixPath, mix);
  if (r != TC_OK) return r;

  // Ghép các đoạn giọng.
  DecodedAudio v;
  for (const auto& p : vocalPaths) {
    DecodedAudio part;
    r = decodeFile(p, part);
    if (r != TC_OK) return r;
    if (v.channels == 0) {
      v.channels = part.channels;
      v.sampleRate = part.sampleRate;
    } else if (part.channels != v.channels || part.sampleRate != v.sampleRate) {
      return TC_ERR_INVALID_ARG;
    }
    v.samples.insert(v.samples.end(), part.samples.begin(), part.samples.end());
    v.frames += part.frames;
  }
  if (v.channels != mix.channels) return TC_ERR_INVALID_ARG;

  // Về tần số bài gốc.
  if (v.sampleRate != mix.sampleRate) {
    const Resampler rs(v.sampleRate, mix.sampleRate);
    v.samples = rs.process(v.samples.data(), static_cast<std::size_t>(v.frames),
                           static_cast<int>(v.channels));
    v.frames = v.samples.size() / v.channels;
    v.sampleRate = mix.sampleRate;
  }
  const uint64_t diff = v.frames > mix.frames ? v.frames - mix.frames : mix.frames - v.frames;
  if (diff > kMaxStemLengthSlack) return TC_ERR_INVALID_ARG;
  v.samples.resize(static_cast<std::size_t>(mix.frames) * mix.channels, 0);
  v.frames = mix.frames;

  // Phần nhạc = bài − giọng.
  instrumental.sampleRate = mix.sampleRate;
  instrumental.channels = mix.channels;
  instrumental.frames = mix.frames;
  instrumental.samples.resize(mix.samples.size());
  for (std::size_t i = 0; i < mix.samples.size(); ++i) {
    const int d = static_cast<int>(mix.samples[i]) - static_cast<int>(v.samples[i]);
    instrumental.samples[i] = static_cast<int16_t>(std::clamp(d, -32768, 32767));
  }
  vocal = std::move(v);
  return TC_OK;
}

}  // namespace tonecore
