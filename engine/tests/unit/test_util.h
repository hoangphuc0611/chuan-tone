#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace tonecore::test {

// Ghi file WAV PCM 16-bit (little-endian) để test bộ giải mã.
inline bool writeWav16(const std::string& path, const std::vector<int16_t>& interleaved,
                       uint32_t sampleRate, uint16_t channels) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const auto dataBytes = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
  auto u32 = [&](uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                static_cast<unsigned char>(v >> 16),
                                static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  auto u16 = [&](uint16_t v) {
    const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
  };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + dataBytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);  // PCM
  u16(channels);
  u32(sampleRate);
  u32(sampleRate * channels * 2);
  u16(static_cast<uint16_t>(channels * 2));
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(dataBytes);
  std::fwrite(interleaved.data(), sizeof(int16_t), interleaved.size(), f);
  return std::fclose(f) == 0;
}

// Sóng sin int16, `channels` kênh giống nhau.
inline std::vector<int16_t> sineInt16(double freq, uint32_t rate, std::size_t frames,
                                      uint16_t channels, double amplitude = 0.5) {
  std::vector<int16_t> v(frames * channels);
  for (std::size_t i = 0; i < frames; ++i) {
    const double s =
        amplitude * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i) / rate);
    for (uint16_t c = 0; c < channels; ++c) {
      v[i * channels + c] = static_cast<int16_t>(std::lround(s * 32767.0));
    }
  }
  return v;
}

inline std::string tempPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / ("tonecore_test_" + name)).string();
}

}  // namespace tonecore::test
