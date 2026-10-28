#pragma once

// Ghi WAV 16-bit cho công cụ dòng lệnh (beatlab, fxlab) — chỉ bản host, không ship.

#include <cstdint>
#include <string>
#include <vector>

namespace tonecore::test {

// `interleaved` float [−1, 1] (ngoài khoảng thì kẹp), `channels` kênh.
bool writeWav(const std::string& path, const std::vector<float>& interleaved, int channels,
              uint32_t rate);

}  // namespace tonecore::test
