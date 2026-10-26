#pragma once

#include <cstdint>
#include <vector>

// Đổi cao độ cả tín hiệu (interleaved) bằng Signalsmith Stretch — chỉ có khi build với
// TC_HAVE_SIGNALSMITH (xem signalsmith_ref.cpp). Kết quả khớp thời gian với bản gốc.
std::vector<float> signalsmithShift(const float* in, uint64_t frames, int channels, double rate,
                                    double semitones);
