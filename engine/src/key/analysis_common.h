#pragma once

#include <functional>

#include "tonecore/tonecore.h"

namespace tonecore {

// Mã lỗi khi phân tích bị huỷ giữa chừng.
inline constexpr tc_result kResultCancelled = TC_ERR_CANCELLED;

// Báo tiến độ 0..1 của một bước phân tích; trả false để huỷ.
using ProgressFn = std::function<bool(float)>;

}  // namespace tonecore
