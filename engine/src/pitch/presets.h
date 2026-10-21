#pragma once

#include "pitch/corrector.h"

namespace tonecore {

// 3 mức "Tự chỉnh giọng" người dùng thấy (CLAUDE.md 3.4). Giá trị khởi đầu, sẽ chỉnh theo nghe A/B.
enum class CorrectionPreset : int32_t { Natural = 0, Singer = 1, Robot = 2 };

inline CorrectorParams correctionPreset(CorrectionPreset p) noexcept {
  CorrectorParams c;
  switch (p) {
    case CorrectionPreset::Natural:  // Tự nhiên: chậm, giữ rung giọng / luyến láy
      c.retuneMs = 40.0f;
      c.humanize = 0.8f;
      c.vibratoPreserve = 1.0f;
      c.flexCents = 10.0f;
      c.hysteresisCents = 30.0f;
      break;
    case CorrectionPreset::Singer:  // Ca sĩ: nhanh, vẫn giữ một phần rung giọng
      c.retuneMs = 15.0f;
      c.humanize = 0.5f;
      c.vibratoPreserve = 0.5f;
      c.flexCents = 0.0f;
      c.hysteresisCents = 20.0f;
      break;
    case CorrectionPreset::Robot:  // Robot: tức thì, bậc thang rõ
      c.retuneMs = 0.0f;
      c.humanize = 0.0f;
      c.vibratoPreserve = 0.0f;
      c.flexCents = 0.0f;
      c.hysteresisCents = 10.0f;
      break;
  }
  return c;
}

}  // namespace tonecore
