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

}  // namespace tonecore
