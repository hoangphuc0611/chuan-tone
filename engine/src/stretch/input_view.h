#pragma once

#include <cstddef>
#include <cstdint>

namespace tonecore {

// Tín hiệu vào của đổi tone: beat interleaved nằm sẵn trong RAM, dạng float hoặc int16 (Track của
// engine — đọc thẳng, không phải chép cả bài sang float: bài 4 phút stereo tốn thêm ~92 MB).
// Ngoài [0, frames) là im lặng.
struct InputView {
  const float* f = nullptr;
  const int16_t* s = nullptr;
  uint64_t frames = 0;
  int channels = 1;

  float at(int64_t t, int c) const noexcept {
    if (t < 0 || static_cast<uint64_t>(t) >= frames) return 0.0f;
    const std::size_t i = static_cast<std::size_t>(t) * static_cast<std::size_t>(channels) +
                          static_cast<std::size_t>(c);
    return f != nullptr ? f[i] : static_cast<float>(s[i]) * (1.0f / 32768.0f);
  }
};

}  // namespace tonecore
