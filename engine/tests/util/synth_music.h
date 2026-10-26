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

// Trống tổng hợp, đánh tức thời (không có attack) tại `timeSec` → biết chính xác onset, dùng để đo
// nhòe trống khi đổi tone:
//  - Kick: sin quét 150 → 50 Hz (τ quét 30 ms), tắt dần τ 50 ms, kèm click 1 ms.
//  - Snare: nhiễu trắng tắt τ 40 ms + âm 185 Hz tắt τ 30 ms.
//  - Hi-hat: nhiễu thông cao (sai phân bậc 1) qua lọc thấp một cực 12 kHz, tắt τ 12 ms.
enum class Drum { Kick, Snare, HiHat };
void addDrum(std::vector<float>& buf, double rate, double timeSec, Drum drum, float amplitude,
             uint32_t seed);

// Vòng hoà âm I–IV–V–I (trưởng) hoặc i–iv–V–i (thứ; V trưởng như thứ hoà âm) ở tone có chủ âm
// `root` (0 = Đô), mỗi hợp âm `chordSec` giây, lặp `loops` vòng từ `startSec`. Voicing ngẫu nhiên
// theo `seed` (thế đảo, vị trí quãng 8 trong G3–G5); nốt bass = gốc hợp âm ở E2–Eb3.
void addCadence(std::vector<float>& buf, double rate, double startSec, int root, bool minor,
                int loops, double chordSec, uint32_t seed, const NoteSpec& spec = {});

}  // namespace tonecore::test
