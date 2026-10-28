#pragma once

// Bộ tín hiệu tổng hợp + chỉ số khách quan cho đổi tone beat (M4). Dùng chung cho unit test và
// `beatlab eval`. Mọi tín hiệu stereo interleaved, có đáp án biết trước (tần số, onset trống).

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace tonecore::test {

struct StretchSignal {
  std::string name;
  double rate = 48000.0;
  std::vector<float> stereo;       // interleaved L, R
  double toneHz = 0.0;             // tần số cơ bản cần đo cao độ (0 = không đo)
  std::vector<double> partialsHz;  // hoạ âm ổn định trong [steadyStart, steadyEnd)
  double steadyStart = 0.0;
  double steadyEnd = 0.0;
  std::vector<double> onsets;  // giây — tiếng trống đứng riêng (đo nhòe)
  bool measureStereo = false;  // đo tương quan hai kênh + LUFS
  bool measureNoise = false;   // nhiễu trắng: đo mức so với lý tưởng

  uint64_t frames() const { return stereo.size() / 2; }
};

// sine 440 Hz · harmonic (La3, 8 hoạ âm) · chord (Đô trưởng + bass) · drums (kick/snare/hi-hat
// đứng riêng, 80 BPM) · beat (trống + hợp âm + bass, có pan stereo) · noise (nhiễu trắng).
std::vector<StretchSignal> stretchSuite(double rate = 48000.0);

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct StretchMetrics {
  double pitchErrorCents = kNaN;  // |tần số đo − toneHz·2^(n/12)| (cent)
  double offPartialDb = kNaN;     // năng lượng ngoài các hoạ âm đã dịch / tổng (dB)
  double preEchoDb = kNaN;        // trung vị: năng lượng 5–50 ms trước onset / đỉnh (dB)
  double preEchoWorstDb = kNaN;   // tiếng trống tệ nhất
  double riseDeltaMs = kNaN;      // trung vị (thời gian lên 10→90% của bản ra − bản gốc)
  // Tệ nhất: bao biên 5 ms của sai phân bậc 1 tăng lại sau đỉnh / đỉnh (dB) — trống "đánh đôi".
  double reboundDb = kNaN;
  double lagMaxMs = kNaN;  // |lệch thời điểm bắt đầu (bao biên vượt 10% đỉnh)| lớn nhất (ms)
  double correlationDelta = kNaN;  // |tương quan L–R bản ra − bản gốc|
  double lufsDelta = kNaN;         // LUFS bản ra − bản gốc
  // Nhiễu trắng: công suất bản ra so với lý tưởng (dB). Lý tưởng = giữ công suất khi hạ tone; khi
  // nâng tone phải bỏ phần trên Nyquist/β (và trên 0,97·Nyquist của bộ lọc) → 10·log10(0,97/β).
  double noiseLevelDb = kNaN;
};

// So bản ra `out` (cùng độ dài, interleaved stereo) với tín hiệu gốc sau khi đổi `semitones`.
StretchMetrics measureStretch(const StretchSignal& sig, const std::vector<float>& out,
                              double semitones);

// Riêng các chỉ số của một tín hiệu khi chưa đổi tone (mốc "gốc" để đọc số tuyệt đối).
StretchMetrics measureReference(const StretchSignal& sig);

// "Bài" không lặp để kiểm nội dung bản đổi tone có ở ĐÚNG CHỖ không (lỗi lặp / lệch đoạn mà vẫn
// mượt thì các chỉ số trên không thấy): mỗi kMelodyNoteSec một nốt mới — tần số giả ngẫu nhiên
// 220–880 Hz, hai kênh khác nhau — cộng tiếng gõ thưa ở thời điểm không đều.
constexpr double kMelodyNoteSec = 0.25;
struct MelodySignal {
  double rate = 48000.0;
  std::vector<float> stereo;  // interleaved L, R
  std::vector<double> noteL;  // Hz, nốt k nằm ở [k, k + 1)·kMelodyNoteSec
  std::vector<double> noteR;
  uint64_t frames() const { return stereo.size() / 2; }
};
MelodySignal melodySignal(double seconds, double rate = 48000.0, uint32_t seed = 1);

// Các nốt (chỉ số k) sai chỗ trong `out` (interleaved stereo; mẫu i của `out` ứng với frame
// outStart + i của bản gốc): tần số trội của một kênh lệch > 1,5% so với nốt k · 2^(n/12). Chỉ xét
// nốt nằm trọn trong `out`.
std::vector<int> misplacedNotes(const MelodySignal& sig, const std::vector<float>& out,
                                double semitones, uint64_t outStart = 0);

}  // namespace tonecore::test
