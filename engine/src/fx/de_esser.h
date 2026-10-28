#pragma once

#include <cstdint>

#include "fx/svf.h"

namespace tonecore {

struct DeEsserParams {
  // Shelf cao động 5 kHz, Q 1: khi giảm hết 6 dB thì 3–4 kHz (độ rõ của giọng) chỉ đổi 0…−0,7 dB,
  // 6,7–9 kHz giảm 5,8–6,4 dB (Q 1/√2 thì 3–4 kHz mất 1,1–1,7 dB) — docs/fx-results.md.
  float shelfHz = 5000.0f;
  float shelfQ = 1.0f;
  float detectLowHz = 5000.0f;  // băng dò "s": 5–9 kHz (CLAUDE.md 3.7)
  float detectHighHz = 9000.0f;
  float thresholdDb = -30.0f;  // mức băng dò (dB, sin toàn thang = 0) bắt đầu giảm
  float ratio = 4.0f;
  float maxReductionDb = 6.0f;  // giảm tối đa (dương): tránh làm "ngọng" s / x / ch / tr
  float attackMs = 1.0f;
  float releaseMs = 50.0f;
};

// De-esser giọng (CLAUDE.md 3.7), mono, tại chỗ, không trễ — "shelf cao động" (dynamic EQ;
// Zölzer, DAFX ch. 4; làm mượt như compressor của Giannoulis 2012):
//  - Sidechain: SVF thông dải trên băng 5–9 kHz (tâm √(lo·hi), Q = tâm/(hi − lo)) → RMS 5 ms → mức
//  L;
//    vượt ngưỡng T thì giảm g_c = max(−tối đa, (L − T)·(1/R − 1)); làm mượt attack / release trong
//    miền dB → g_s.
//  - Đường chính: SVF high shelf (shelfHz, Q 1/√2) với độ lợi g_s dB, hệ số tính lại mỗi 16 mẫu.
//    g_s = 0 → shelf đúng bằng 1 (m0 = 1, m1 = m2 = 0) → ra = vào từng mẫu (giọng không có "s" đi
//    thẳng); bộ lọc vẫn chạy để không giật khi bắt đầu giảm.
// Đã thử tách băng bù trừ y = x − (1 − g)·thông_cao(x): gần tần số cắt phần cao lệch pha (~67° ở
// 6,7 kHz khi cắt 5 kHz) nên phép trừ chỉ giảm ~1 dB thay vì 6 dB → bỏ. Cấp phát: không.
class DeEsser {
 public:
  explicit DeEsser(double sampleRate);

  void setParams(const DeEsserParams& p) noexcept;  // [RT]
  void reset() noexcept;
  void process(float* x, uint32_t n) noexcept;  // [RT]

  float gainReductionDb() const noexcept { return gs_; }  // ≤ 0
  static constexpr double kRmsSec = 0.005;

 private:
  static constexpr uint32_t kSubBlock = 16;

  double fs_;
  DeEsserParams p_;
  SvfCoeffs shelf_;
  SvfCoeffs detect_;
  Svf shelfFilter_;
  Svf detectFilter_;
  float shelfGainDb_ = 0.0f;  // độ lợi đang dùng trong shelf_
  float rmsCoeff_ = 1.0f;
  float attack_ = 0.0f;
  float release_ = 0.0f;
  float ms_ = 0.0f;
  float gs_ = 0.0f;  // dB
};

}  // namespace tonecore
