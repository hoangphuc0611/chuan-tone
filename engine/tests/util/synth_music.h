#pragma once

// Nhạc tổng hợp có đáp án biết trước (cao độ, tuning, thời điểm gõ) cho test dò tone / tuning /
// chroma / HPSS. Nhạc cụ cộng hoạ âm: biên độ hoạ âm h = 1/h, bao biên attack 10 ms, release
// 50 ms, có thể tắt dần theo hàm mũ. Chỉ dùng cho test / bench / công cụ.

#include <cstdint>
#include <vector>

namespace tonecore::test {

// MIDI (số thực) → Hz, lưới La4 = 440 Hz dời `tuningCents`.
double midiToHz(double midi, double tuningCents = 0.0);

struct NoteSpec {
  int harmonics = 6;
  double decayPerSec = 0.0;  // 0 = ngân đều (kiểu đàn dây/organ); > 0 = tắt dần (kiểu piano)
  double tuningCents = 0.0;
};

// Cộng một nốt vào `buf` (mono). Hoạ âm vượt 0,45·rate bị bỏ.
void addNote(std::vector<float>& buf, double rate, double startSec, double durSec, double midi,
             float amplitude, const NoteSpec& spec = {});

// Tiếng gõ: nhiễu trắng tắt dần nhanh (τ = 4 ms), băng rộng — "đường dọc" trên spectrogram.
void addClick(std::vector<float>& buf, double rate, double timeSec, float amplitude, uint32_t seed);

}  // namespace tonecore::test
