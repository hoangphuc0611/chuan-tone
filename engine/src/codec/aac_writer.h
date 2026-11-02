#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "tonecore/tonecore.h"

namespace tonecore {

// Ghi AAC-LC trong M4A bằng bộ nén của hệ điều hành (M7, CLAUDE.md mục 3.8): iOS / macOS
// ExtAudioFile, Android NDK AMediaCodec + AMediaMuxer. Không có thư viện bên thứ ba. Ghi vào
// `<path>.part`, finish() mới đổi tên. Chạy ở luồng nền.
class AacWriter {
 public:
  virtual ~AacWriter() = default;
  // `interleaved`: frames × channels mẫu float (−1..1; ngoài khoảng bị kẹp).
  virtual bool write(const float* interleaved, std::size_t frames) = 0;
  // Đẩy nốt dữ liệu trong bộ nén, đóng file, đổi tên. false = lỗi (file tạm bị xoá).
  virtual bool finish() = 0;
  // Đóng và xoá file tạm.
  virtual void abort() = 0;
};

// nullptr + `err` khi nền tảng không có bộ nén (Linux: TC_ERR_NOT_SUPPORTED) hoặc không mở được
// file / bộ nén.
std::unique_ptr<AacWriter> createAacWriter(const std::string& path, uint32_t sampleRate,
                                           uint32_t channels, int32_t bitrate, tc_result& err);

}  // namespace tonecore
