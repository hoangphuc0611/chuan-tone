#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "decode/decoder.h"
#include "record/wav_file.h"

namespace tonecore {

// Beat đã sẵn sàng phát: PCM int16 stereo interleaved, đúng tần số của engine.
struct Track {
  // Beat; hoặc phần nhạc (bài gốc − giọng) khi bài có stem giọng.
  std::vector<int16_t> pcm;
  // (M6) Stem giọng ca sĩ, cùng dạng / độ dài với pcm; rỗng = không có. Phát pcm + k · vocal
  // (k = "Giữ giọng ca sĩ gốc": 0 = karaoke, 1 = như bài gốc).
  std::vector<int16_t> vocal;
  // (M7) Nghe lại bản thu: giọng khô (mono float) đã căn với beat — voice[n] hát cùng beat frame
  // n; dài bằng pcm. Không trộn thẳng vào beat: engine đưa nó vào chuỗi giọng thay cho micro.
  std::vector<float> voice;
  uint64_t frames = 0;

  bool hasVocal() const noexcept { return !vocal.empty(); }
  bool hasVoice() const noexcept { return !voice.empty(); }
};

// Chuyển dữ liệu đã giải mã thành Track: đổi sang stereo (mono → nhân đôi, > 2 kênh → lấy 2 kênh
// đầu) và resample về `engineRate` nếu cần. Chạy ở luồng nền.
std::unique_ptr<Track> prepareTrack(DecodedAudio&& decoded, uint32_t engineRate);

// Như prepareTrack cho phần nhạc + giọng (decodeStems: cùng tần số, số kênh, độ dài) → một Track.
std::unique_ptr<Track> prepareTrackWithVocal(DecodedAudio&& instrumental, DecodedAudio&& vocal,
                                             uint32_t engineRate);

// (M7) Bản thu để nghe lại: beat.wav + giọng khô (kênh đầu của dry.wav) dịch sớm
// `voiceOffsetFrames` frame (tính ở tần số của dry.wav): voice[n] = dry[n + offset], ngoài file =
// 0. Đổi về `engineRate` nếu cần.
std::unique_ptr<Track> prepareTake(DecodedAudio&& beat, const WavData& dry,
                                   int64_t voiceOffsetFrames, uint32_t engineRate);

}  // namespace tonecore
