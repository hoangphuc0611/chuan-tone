#pragma once

#include <cstdint>

#include "fx/fx_chain.h"

namespace tonecore {

// Preset FX theo thể loại (CLAUDE.md mục 6, docs/m5-plan.md mục 5). Giá trị khởi đầu — tinh chỉnh
// bằng tai ở B6. Mức chỉnh giọng của từng thể loại do app đặt (API riêng).
enum class Genre : int32_t {
  Dry = 0,
  Bolero = 1,
  Ballad = 2,
  Pop = 3,
  Remix = 4,
  Rap = 5,
  Lofi = 6
};

FxPreset genrePreset(Genre g) noexcept;

}  // namespace tonecore
