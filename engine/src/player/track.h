#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "decode/decoder.h"

namespace tonecore {

// Beat đã sẵn sàng phát: PCM int16 stereo interleaved, đúng tần số của engine.
struct Track {
  std::vector<int16_t> pcm;
  uint64_t frames = 0;
};

// Chuyển dữ liệu đã giải mã thành Track: đổi sang stereo (mono → nhân đôi, > 2 kênh → lấy 2 kênh
// đầu) và resample về `engineRate` nếu cần. Chạy ở luồng nền.
std::unique_ptr<Track> prepareTrack(DecodedAudio&& decoded, uint32_t engineRate);

}  // namespace tonecore
