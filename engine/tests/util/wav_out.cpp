#include "util/wav_out.h"

#include <cmath>

#include "dr_wav.h"

namespace tonecore::test {

bool writeWav(const std::string& path, const std::vector<float>& interleaved, int channels,
              uint32_t rate) {
  drwav_data_format fmt{};
  fmt.container = drwav_container_riff;
  fmt.format = DR_WAVE_FORMAT_PCM;
  fmt.channels = static_cast<drwav_uint32>(channels);
  fmt.sampleRate = rate;
  fmt.bitsPerSample = 16;
  drwav wav;
  if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr)) return false;
  std::vector<int16_t> pcm(interleaved.size());
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const float v = std::fmax(-1.0f, std::fmin(1.0f, interleaved[i]));
    pcm[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
  }
  const auto frames = static_cast<drwav_uint64>(pcm.size() / static_cast<std::size_t>(channels));
  const drwav_uint64 written = drwav_write_pcm_frames(&wav, frames, pcm.data());
  drwav_uninit(&wav);
  return written == frames;
}

}  // namespace tonecore::test
