#pragma once

#include <cstdint>
#include <vector>

#include "fx/compressor.h"
#include "fx/de_esser.h"
#include "fx/delay.h"
#include "fx/equalizer.h"
#include "fx/reverb.h"

namespace tonecore {

// Một bộ thông số FX giọng (preset). Khối nào tắt thì bỏ qua (không tốn CPU, không đổi mẫu).
// Mặc định: mọi khối tắt = preset "Khô".
struct FxPreset {
  EqParams eq;  // tắt khi mọi băng Off và không có thông cao / thông thấp
  bool compOn = false;
  CompressorParams comp;
  bool deessOn = false;
  DeEsserParams deess;
  DelayParams delay{NoteDivision::Off};  // Off = tắt; bpm do engine điền (tc_set_tempo)
  bool reverbOn = false;
  ReverbParams reverb;

  bool eqOn() const noexcept;
};

// Hai preset khác nhau về "cấu trúc" — thứ không trượt dần được: bật / tắt khối, loại băng EQ,
// thời gian delay (phần nốt, ms, BPM), kiểu phòng, pre-delay → phải dựng chuỗi mới (đổi preset hai
// chuỗi, docs/m5-plan.md điểm 1–2). Còn lại (dB, tần số, Q, ngưỡng, tỉ lệ, độ ướt, hồi tiếp, RT60,
// damping…) cập nhật ngay trên chuỗi đang chạy và trượt dần.
bool needsRebuild(const FxPreset& a, const FxPreset& b) noexcept;

// Chuỗi FX giọng: phần KHÔ = EQ → compressor → de-esser (mono); phần ƯỚT = delay(khô · gửi) và
// reverb(khô · gửi + vọng) (stereo). Đầu ra tách khô / ướt để FxRack crossfade phần khô và để đuôi
// ướt của chuỗi cũ ngân nốt. Cấp phát: chỉ trong constructor.
class FxChain {
 public:
  FxChain(double sampleRate, uint32_t maxBlockFrames, const FxPreset& preset, uint64_t generation);

  // [RT] Cập nhật thông số liên tục (cùng cấu trúc — needsRebuild(preset(), p) == false).
  void applyContinuous(const FxPreset& p) noexcept;

  // [RT] n ≤ maxBlockFrames mẫu. in = nullptr: không có tiếng vào (chỉ còn đuôi). Phần khô chỉ tính
  // khi withDry; lượng gửi sang delay / reverb trượt tuyến tính sendFrom → sendTo trong n mẫu.
  void process(const float* in, uint32_t n, bool withDry, float sendFrom, float sendTo) noexcept;

  const float* dry() const noexcept { return dry_.data(); }
  const float* wetL() const noexcept { return wetL_.data(); }
  const float* wetR() const noexcept { return wetR_.data(); }
  // Trung bình bình phương phần ướt của khối vừa xử lý (dò đuôi đã tắt).
  float wetMeanSquare() const noexcept { return wetMs_; }
  bool hasWet() const noexcept { return delayOn_ || preset_.reverbOn; }

  const FxPreset& preset() const noexcept { return preset_; }
  uint64_t generation() const noexcept { return generation_; }

 private:
  FxPreset preset_;
  uint64_t generation_;
  bool eqOn_;
  bool delayOn_;
  Equalizer eq_;
  Compressor comp_;
  DeEsser deess_;
  Delay delay_;
  Reverb reverb_;
  std::vector<float> dry_;
  std::vector<float> send_;
  std::vector<float> echo_;
  std::vector<float> revIn_;
  std::vector<float> wetL_;
  std::vector<float> wetR_;
  float wetMs_ = 0.0f;
};

}  // namespace tonecore
