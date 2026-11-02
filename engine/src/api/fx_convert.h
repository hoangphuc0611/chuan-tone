#pragma once

#include "fx/fx_presets.h"
#include "tonecore/tonecore.h"

namespace tonecore::capi {

// Mọi số thực trong preset đều hữu hạn (NaN / vô cực → TC_ERR_INVALID_ARG ở C API).
bool allFinite(const tc_fx_preset& c);
// tc_fx_preset → FxPreset, kẹp mọi giá trị vào khoảng hợp lệ.
FxPreset toFx(const tc_fx_preset& c);
void fromFx(const FxPreset& p, tc_fx_preset& c);

}  // namespace tonecore::capi
