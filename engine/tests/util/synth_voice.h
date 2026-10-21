#pragma once

// Bộ tạo giọng hát tổng hợp có cao độ biết trước — "đáp án" cho test dò cao độ / chỉnh giọng.
//
// Mô hình nguồn–lọc (source–filter, Fant 1960):
//   nguồn  = đạo hàm xung thanh môn Rosenberg (A. Rosenberg, "Effect of glottal pulse shape on
//            the quality of natural vowels", JASA 1971), có jitter (dao động chu kỳ) và shimmer
//            (dao động biên độ) như giọng thật;
//   bộ lọc = 3 bộ cộng hưởng formant bậc 2 mắc nối tiếp (nguyên âm a / i / u);
//   cộng thêm nhiễu hơi thở.
// Chỉ dùng cho test / bench / công cụ, không ship trong app.

#include <cstdint>
#include <functional>
#include <vector>

namespace tonecore::test {

enum class Vowel { A, I, U };

// Đường cao độ: thời điểm (giây) → f0 (Hz). Trả về 0 nghĩa là không phát âm (vô thanh).
using F0Contour = std::function<double(double)>;

struct VoiceSpec {
  double sampleRate = 48000.0;
  Vowel vowel = Vowel::A;
  double jitter = 0.004;      // độ lệch chuẩn tương đối của mỗi chu kỳ
  double shimmer = 0.03;      // độ lệch chuẩn tương đối của biên độ mỗi chu kỳ
  double breathDb = -40.0;    // nhiễu hơi thở so với biên độ đỉnh
  double amplitude = 0.3;     // biên độ đỉnh sau chuẩn hoá
  uint32_t seed = 1;
};

struct SynthResult {
  std::vector<float> audio;  // mono
  std::vector<float> f0;     // f0 "đáp án" theo từng mẫu (Hz, 0 = vô thanh)
};

SynthResult synthVoice(const F0Contour& contour, double durationSec, const VoiceSpec& spec = {});

// Nhiễu trắng / tiếng xát "s" (nhiễu qua lọc thông cao 4 kHz) — để test nhận diện vô thanh.
std::vector<float> whiteNoise(double durationSec, double sampleRate, double amplitude,
                              uint32_t seed = 3);
std::vector<float> fricativeS(double durationSec, double sampleRate, double amplitude,
                              uint32_t seed = 5);

double midiToHz(double midi);
double hzToMidi(double hz);

// ---- Các đường cao độ mẫu ----
F0Contour steady(double hz);
// Rung giọng hình sin quanh `hz`.
F0Contour vibrato(double hz, double rateHz = 5.5, double depthCents = 50.0);
// Trượt từ `fromHz` tới `toHz` (theo thang log) trong [start, start + dur], giữ nguyên ngoài đoạn.
F0Contour glide(double fromHz, double toHz, double startSec, double durSec);
// Lệch cố định `cents` so với `hz`.
F0Contour offset(double hz, double cents);

// 6 thanh điệu tiếng Việt (xấp xỉ theo mô tả ngữ âm học phổ biến, đơn vị nửa cung so với
// cao độ nền), mỗi âm tiết dài `syllableSec`. Thanh nặng kết thúc bằng tắc thanh hầu (vô thanh).
enum class VietTone { Ngang, Huyen, Sac, Hoi, Nga, Nang };
F0Contour vietTone(VietTone tone, double baseHz, double syllableSec = 0.4);
// Hình dạng thanh điệu (nửa cung) tại vị trí p ∈ [0, 1] trong âm tiết; NaN nếu vô thanh.
double vietToneShape(VietTone tone, double p);

}  // namespace tonecore::test
