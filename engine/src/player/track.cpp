#include "player/track.h"

#include <algorithm>

#include "dsp/resampler.h"

namespace tonecore {

std::unique_ptr<Track> prepareTrack(DecodedAudio&& decoded, uint32_t engineRate) {
  if (decoded.frames == 0 || decoded.channels == 0 || decoded.sampleRate == 0) return nullptr;

  // Bước 1: về stereo.
  std::vector<int16_t> stereo;
  const auto frames = static_cast<std::size_t>(decoded.frames);
  if (decoded.channels == 2) {
    stereo = std::move(decoded.samples);
  } else {
    stereo.resize(frames * 2);
    const std::size_t ch = decoded.channels;
    for (std::size_t i = 0; i < frames; ++i) {
      const int16_t l = decoded.samples[i * ch];
      const int16_t r = ch >= 2 ? decoded.samples[i * ch + 1] : l;
      stereo[2 * i] = l;
      stereo[2 * i + 1] = r;
    }
    decoded.samples = {};  // giải phóng sớm
  }

  auto track = std::make_unique<Track>();
  // Bước 2: resample nếu khác tần số engine.
  if (decoded.sampleRate == engineRate) {
    track->pcm = std::move(stereo);
    track->frames = frames;
  } else {
    Resampler rs(decoded.sampleRate, engineRate);
    track->pcm = rs.process(stereo.data(), frames, 2);
    track->frames = track->pcm.size() / 2;
  }
  return track;
}

std::unique_ptr<Track> prepareTrackWithVocal(DecodedAudio&& instrumental, DecodedAudio&& vocal,
                                             uint32_t engineRate) {
  auto track = prepareTrack(std::move(instrumental), engineRate);
  auto v = prepareTrack(std::move(vocal), engineRate);
  if (!track || !v) return nullptr;
  // Cùng độ dài vào + cùng bộ đổi tần số → cùng độ dài ra; vẫn chốt cho chắc.
  const uint64_t frames = std::min(track->frames, v->frames);
  track->frames = frames;
  track->pcm.resize(static_cast<std::size_t>(frames) * 2);
  v->pcm.resize(static_cast<std::size_t>(frames) * 2);
  track->vocal = std::move(v->pcm);
  return track;
}

std::unique_ptr<Track> prepareTake(DecodedAudio&& beat, const WavData& dry,
                                   int64_t voiceOffsetFrames, uint32_t engineRate) {
  auto track = prepareTrack(std::move(beat), engineRate);
  if (!track || dry.frames == 0 || dry.channels == 0 || dry.sampleRate == 0) return nullptr;
  // Kênh đầu, dịch sớm ở tần số gốc của bản thu (độ lệch đo ở tần số đó).
  const auto n = static_cast<int64_t>(dry.frames);
  std::vector<float> mono(static_cast<std::size_t>(n), 0.0f);
  for (int64_t i = 0; i < n; ++i) {
    const int64_t src = i + voiceOffsetFrames;
    if (src >= 0 && src < n) mono[i] = dry.samples[static_cast<std::size_t>(src) * dry.channels];
  }
  if (dry.sampleRate != engineRate) {
    Resampler rs(dry.sampleRate, engineRate);
    mono = rs.process(mono.data(), mono.size(), 1);
  }
  mono.resize(static_cast<std::size_t>(track->frames), 0.0f);  // dài bằng beat
  track->voice = std::move(mono);
  return track;
}

}  // namespace tonecore
