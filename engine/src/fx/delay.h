#pragma once

#include <cstdint>
#include <vector>

#include "fx/svf.h"

namespace tonecore {

enum class NoteDivision : int32_t {
  Off = 0,
  Quarter = 1,       // 1/4 nốt = 1 phách
  Eighth = 2,        // 1/8
  DottedEighth = 3,  // 1/8 chấm = 3/4 phách
  Sixteenth = 4,     // 1/16
  Milliseconds = 5,  // theo timeMs
};

struct DelayParams {
  NoteDivision division = NoteDivision::Eighth;
  float timeMs = 300.0f;  // khi division = Milliseconds
  float bpm = 100.0f;
  float feedback = 0.25f;     // 0…0,5 (CLAUDE.md rủi ro rú: giới hạn 50%)
  float mix = 0.2f;           // độ lớn phần vọng so với tiếng khô
  float lowPassHz = 4000.0f;  // trong vòng hồi tiếp (0 = tắt): mỗi lần vọng tối hơn
  float highPassHz = 200.0f;  // trong vòng hồi tiếp (0 = tắt): vọng không ù
};

// Delay / echo giọng (CLAUDE.md 3.7), mono, chỉ xuất phần VỌNG (phần khô do chuỗi FX giữ) — để khi
// đổi preset phần vọng cũ ngân nốt (docs/m5-plan.md điểm 1). Zölzer, DAFX ch. 2:
//   r[n] = dòng_trễ(n − D)            (D = 60/BPM × phần nốt × fs, lẻ mẫu: nội suy Lagrange bậc 3)
//   f[n] = HPF(LPF(r[n]))             (SVF bậc 2, chỉ trong đường vọng)
//   dòng_trễ ← x[n] + hồi_tiếp · f[n]
//   vọng[n] = mix · f[n]
// → tiếng vọng thứ k đến sau k·D, nhỏ dần hồi_tiếp^(k−1), mỗi lần qua bộ lọc một lần (tối dần).
// mix và hồi tiếp trượt τ 20 ms; đổi thời gian trễ / bộ lọc thì áp ngay (khi đang phát, đi đường
// đổi preset của chuỗi FX). Cấp phát: chỉ trong constructor.
class Delay {
 public:
  static constexpr double kMaxSeconds = 2.0;  // 1/4 nốt ở 30 BPM
  static constexpr float kMaxFeedback = 0.5f;

  explicit Delay(double sampleRate);

  void setParams(const DelayParams& p) noexcept;  // [RT]
  void reset() noexcept;
  // [RT] in: giọng (mono); wet: phần vọng (ghi đè).
  void process(const float* in, float* wet, uint32_t n) noexcept;

  double delaySamples() const noexcept { return delay_; }
  static double delaySeconds(const DelayParams& p) noexcept;

 private:
  double fs_;
  std::vector<float> buf_;
  uint32_t mask_ = 0;
  uint32_t write_ = 0;
  double delay_ = 0.0;  // mẫu
  bool off_ = true;
  SvfCoeffs lpf_;
  SvfCoeffs hpf_;
  bool useLpf_ = false;
  bool useHpf_ = false;
  Svf lpfState_;
  Svf hpfState_;
  float smooth_ = 1.0f;
  float feedback_ = 0.0f;
  float feedbackTarget_ = 0.0f;
  float mix_ = 0.0f;
  float mixTarget_ = 0.0f;
};

}  // namespace tonecore
