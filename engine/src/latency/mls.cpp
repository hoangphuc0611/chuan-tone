#include "latency/mls.h"

#include <cstdint>

namespace tonecore {

namespace {

// Vị trí tap (đánh số từ 1) của đa thức nguyên thủy cho từng bậc, theo bảng LFSR tối đa
// trong Xilinx XAPP052 ("Efficient Shift Registers, LFSR Counters, and Long Pseudo-Random
// Sequence Generators"). Test kiểm tra lại chu kỳ và tự tương quan.
struct Taps {
  int order;
  int taps[4];
  int count;
};

constexpr Taps kTaps[] = {
    {8, {8, 6, 5, 4}, 4},   {9, {9, 5, 0, 0}, 2},    {10, {10, 7, 0, 0}, 2},
    {11, {11, 9, 0, 0}, 2}, {12, {12, 6, 4, 1}, 4},  {13, {13, 4, 3, 1}, 4},
    {14, {14, 5, 3, 1}, 4}, {15, {15, 14, 0, 0}, 2}, {16, {16, 15, 13, 4}, 4},
};

}  // namespace

std::vector<float> generateMls(int order) {
  const Taps* t = nullptr;
  for (const auto& candidate : kTaps) {
    if (candidate.order == order) t = &candidate;
  }
  if (t == nullptr) return {};

  // Đa thức x^n + Σ x^a + 1 ↔ công thức truy hồi s[i+n] = s[i] ⊕ Σ s[i+a].
  // Bit j của trạng thái giữ s[i+j] → mặt nạ gồm bit 0 và bit a của mỗi tap a < n.
  uint32_t mask = 1u;
  for (int i = 0; i < t->count; ++i) {
    if (t->taps[i] != order) mask |= 1u << t->taps[i];
  }

  const uint32_t length = (1u << order) - 1;
  const uint32_t topBit = 1u << (order - 1);
  std::vector<float> out(length);

  // LFSR dạng Fibonacci: bit mới = XOR các bit tap; trạng thái khởi đầu khác 0.
  uint32_t state = 1;
  for (uint32_t i = 0; i < length; ++i) {
    out[i] = (state & 1u) != 0 ? 1.0f : -1.0f;
    const uint32_t feedback = static_cast<uint32_t>(__builtin_parity(state & mask));
    state = (state >> 1) | (feedback != 0 ? topBit : 0u);
  }
  return out;
}

}  // namespace tonecore
