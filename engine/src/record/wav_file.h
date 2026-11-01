#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "tonecore/tonecore.h"

namespace tonecore {

enum class WavFormat {
  Float32,  // IEEE float 32-bit (WAVE_FORMAT_IEEE_FLOAT, có chunk "fact")
  Int16     // PCM 16-bit
};

// Ghi WAV tuần tự cho bản thu (M7). Ghi vào `<path>.part`, header được sửa khi finish() rồi mới
// đổi tên thành `path` → file thấy được luôn là file hoàn chỉnh. Không dùng từ audio thread.
class WavWriter {
 public:
  WavWriter() = default;
  ~WavWriter() { abort(); }
  WavWriter(const WavWriter&) = delete;
  WavWriter& operator=(const WavWriter&) = delete;

  tc_result open(const std::string& path, uint32_t channels, uint32_t sampleRate, WavFormat format);
  // `interleaved`: frames × channels mẫu float. Int16: kẹp ±1 rồi làm tròn.
  bool write(const float* interleaved, std::size_t frames);
  // Sửa header, đóng, đổi tên. false nếu lỗi ghi (file tạm bị xoá).
  bool finish();
  // Đóng và xoá file tạm (không làm gì nếu chưa mở).
  void abort();

  bool isOpen() const noexcept { return file_ != nullptr; }
  uint64_t frames() const noexcept { return frames_; }

 private:
  bool writeHeader();

  std::FILE* file_ = nullptr;
  std::string path_;
  uint32_t channels_ = 0;
  uint32_t sampleRate_ = 0;
  WavFormat format_ = WavFormat::Int16;
  uint64_t frames_ = 0;
  bool failed_ = false;
  std::vector<uint8_t> scratch_;
};

// File WAV đã đọc ra float interleaved (−1..1).
struct WavData {
  std::vector<float> samples;
  uint32_t sampleRate = 0;
  uint32_t channels = 0;
  uint64_t frames = 0;
};

// Đọc WAV PCM 16 / 24 / 32-bit hoặc float 32-bit (đủ cho file bản thu do WavWriter ghi; giữ
// nguyên độ phân giải float, khác decodeFile chỉ trả 16-bit). Lỗi → TC_ERR_INVALID_ARG /
// TC_ERR_NOT_SUPPORTED.
tc_result readWav(const std::string& path, WavData& out);

}  // namespace tonecore
