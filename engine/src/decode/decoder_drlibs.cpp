// Bộ giải mã dựa trên dr_wav / dr_mp3 (public domain / MIT-0) — dùng cho bản host (test, CLI,
// CI Linux). App trên điện thoại dùng codec của hệ điều hành.

#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_WAV_NO_STDIO_WIDE  // không cần đường dẫn wchar_t
#include "decode/decoder.h"

#include <cstring>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "dr_mp3.h"
#include "dr_wav.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace tonecore {

namespace {

bool hasSuffix(const std::string& s, const char* suffix) {
  const std::size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (std::size_t i = 0; i < n; ++i) {
    char c = s[s.size() - n + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

tc_result checkLength(uint64_t frames, uint32_t rate) {
  if (rate == 0) return TC_ERR_NOT_SUPPORTED;
  if (static_cast<double>(frames) / rate > kMaxBeatSeconds) return TC_ERR_NOT_SUPPORTED;
  return TC_OK;
}

}  // namespace

tc_result decodeFileDrLibs(const std::string& path, DecodedAudio& out) {
  out = DecodedAudio{};

  if (hasSuffix(path, ".mp3")) {
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path.c_str(), nullptr)) return TC_ERR_INVALID_ARG;
    const drmp3_uint64 frames = drmp3_get_pcm_frame_count(&mp3);
    const tc_result lenCheck = checkLength(frames, mp3.sampleRate);
    if (lenCheck != TC_OK || mp3.channels == 0) {
      drmp3_uninit(&mp3);
      return TC_ERR_NOT_SUPPORTED;
    }
    out.sampleRate = mp3.sampleRate;
    out.channels = mp3.channels;
    out.samples.resize(static_cast<std::size_t>(frames) * mp3.channels);
    out.frames = drmp3_read_pcm_frames_s16(&mp3, frames, out.samples.data());
    out.samples.resize(static_cast<std::size_t>(out.frames) * out.channels);
    drmp3_uninit(&mp3);
    return out.frames > 0 ? TC_OK : TC_ERR_NOT_SUPPORTED;
  }

  drwav wav;
  if (!drwav_init_file(&wav, path.c_str(), nullptr)) return TC_ERR_INVALID_ARG;
  if (checkLength(wav.totalPCMFrameCount, wav.sampleRate) != TC_OK || wav.channels == 0) {
    drwav_uninit(&wav);
    return TC_ERR_NOT_SUPPORTED;
  }
  out.sampleRate = wav.sampleRate;
  out.channels = wav.channels;
  out.samples.resize(static_cast<std::size_t>(wav.totalPCMFrameCount) * wav.channels);
  out.frames = drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, out.samples.data());
  out.samples.resize(static_cast<std::size_t>(out.frames) * out.channels);
  drwav_uninit(&wav);
  return out.frames > 0 ? TC_OK : TC_ERR_NOT_SUPPORTED;
}

#if !defined(__APPLE__) && !defined(__ANDROID__)
// Linux/host khác: dùng dr_libs làm bộ giải mã chính.
tc_result decodeFile(const std::string& path, DecodedAudio& out) {
  return decodeFileDrLibs(path, out);
}
#endif

}  // namespace tonecore
