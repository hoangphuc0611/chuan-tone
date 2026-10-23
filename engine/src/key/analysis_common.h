#pragma once

#include <functional>

#include "tonecore/tonecore.h"

namespace tonecore {

// Mã lỗi khi phân tích bị huỷ giữa chừng. Sẽ thành TC_ERR_CANCELLED trong C API 0.4 (M3 bước B7).
inline constexpr tc_result kResultCancelled = -7;

// Báo tiến độ 0..1 của một bước phân tích; trả false để huỷ.
using ProgressFn = std::function<bool(float)>;

}  // namespace tonecore
