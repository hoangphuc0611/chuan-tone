#pragma once

#include <cstdint>

#include "fx/svf.h"

namespace tonecore {

struct EqBand {
  SvfType type = SvfType::Off;  // Bell, LowShelf, HighShelf (hoặc Off)
  float freqHz = 1000.0f;
  float gainDb = 0.0f;
  float q = 0.7071f;
};

struct EqParams {
  static constexpr int kBands = 5;
  float highPassHz = 0.0f;  // 0 = tắt; bậc 2 Butterworth (Q = 1/√2)
  float lowPassHz = 0.0f;   // 0 = tắt
  EqBand bands[kBands];
};

// EQ giọng (CLAUDE.md 3.7): thông cao + thông thấp + 5 băng bell / shelf, nối tiếp, mono, tại chỗ.
// Mỗi tầng là một SVF dạng TPT (fx/svf.h): đáp ứng như RBJ cookbook nhưng đổi thông số khi đang
// chạy không nổ, không "lụp bụp". Tần số / dB / Q trượt dần về đích (một cực, τ ≈ 20 ms; tần số
// trượt theo thang log), hệ số tính lại mỗi khối 16 mẫu và chỉ khi đang trượt. Bật / tắt tầng hay
// đổi loại băng thì áp ngay — khi đang phát, việc đó đi đường đổi preset (crossfade cả chuỗi FX, M5
// B5).
//
// Cấp phát: không. setParams()/process() gọi từ cùng một luồng (audio thread).
class Equalizer {
 public:
  explicit Equalizer(double sampleRate, double smoothingSec = 0.02);

  // [RT] Thông số mới: tần số / dB / Q trượt dần; bật / tắt, loại băng áp ngay.
  void setParams(const EqParams& p) noexcept;
  // [RT] Áp ngay không trượt (dựng chuỗi mới, test).
  void setParamsImmediate(const EqParams& p) noexcept;
  void reset() noexcept;
  // [RT] Xử lý mono tại chỗ.
  void process(float* x, uint32_t n) noexcept;

  // Đáp ứng biên độ hiện tại (tích các tầng) tại f (Hz) — cho test / hiển thị.
  double magnitudeAt(double f) const noexcept;

 private:
  static constexpr int kStages =
      EqParams::kBands + 2;  // [0] thông cao, [1..5] băng, [6] thông thấp
  static constexpr uint32_t kSubBlock = 16;

  struct Stage {
    SvfType type = SvfType::Off;
    double logFreq = 0.0;  // hiện tại (ln Hz)
    double gainDb = 0.0;
    double q = 0.7071;
    double logFreqTarget = 0.0;
    double gainTarget = 0.0;
    double qTarget = 0.7071;
    bool moving = false;
    SvfCoeffs c;
    Svf f;
  };

  void setStageTarget(Stage& s, SvfType type, double freq, double gainDb, double q) noexcept;
  void updateCoeffs(Stage& s) noexcept;
  bool active(const Stage& s) const noexcept;

  double fs_;
  double smooth_;  // hệ số một cực cho mỗi khối kSubBlock mẫu
  Stage stages_[kStages];
};

}  // namespace tonecore
