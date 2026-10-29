#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tonecore/tonecore.h"

namespace tonecore {

// PCM 16-bit interleaved sau khi giải mã.
struct DecodedAudio {
  std::vector<int16_t> samples;
  uint32_t sampleRate = 0;
  uint32_t channels = 0;
  uint64_t frames = 0;
};

// Bài dài hơn mức này bị từ chối để tránh hết RAM (int16 stereo 48 kHz 12 phút ≈ 138 MB).
inline constexpr double kMaxBeatSeconds = 12.0 * 60.0;

// Giải mã file bằng codec của hệ điều hành:
//  - Android: AMediaExtractor + AMediaCodec (NDK)
//  - iOS/macOS: ExtAudioFile (AudioToolbox)
//  - Linux (chỉ test/CI, server): dr_wav / dr_mp3 / dr_flac
// Trả về TC_OK hoặc mã lỗi TC_ERR_* (TC_ERR_INVALID_ARG khi không mở được file,
// TC_ERR_NOT_SUPPORTED khi định dạng không hỗ trợ hoặc file quá dài).
// Gọi từ luồng nền, không bao giờ từ audio thread.
tc_result decodeFile(const std::string& path, DecodedAudio& out);

// Bộ giải mã dr_libs (WAV/MP3/FLAC) — luôn có trên bản host để test so sánh.
tc_result decodeFileDrLibs(const std::string& path, DecodedAudio& out);

}  // namespace tonecore
