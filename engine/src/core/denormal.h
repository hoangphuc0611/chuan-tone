#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define TC_DENORMAL_X86 1
#endif

namespace tonecore {

// Bật chế độ flush-to-zero trong phạm vi một khối lệnh (RAII).
//
// Số denormal (rất gần 0) xuất hiện ở đuôi reverb/filter khi tín hiệu tắt dần; trên nhiều CPU
// phép tính với chúng chậm hơn hàng chục lần → gây xrun. Đặt cờ FTZ/DAZ ở đầu audio callback
// để phần cứng coi chúng là 0. [RT]
class ScopedFlushDenormals {
 public:
  ScopedFlushDenormals() noexcept {
#if defined(TC_DENORMAL_X86)
    saved_ = _mm_getcsr();
    _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040u);  // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    saved_ = fpcr;
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));  // FZ
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    saved_ = fpscr;
    __asm__ __volatile__("vmsr fpscr, %0" ::"r"(fpscr | (1u << 24)));  // FZ
#endif
  }

  ~ScopedFlushDenormals() noexcept {
#if defined(TC_DENORMAL_X86)
    _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(__aarch64__)
    __asm__ __volatile__("msr fpcr, %0" ::"r"(saved_));
#elif defined(__arm__) && defined(__ARM_FP)
    __asm__ __volatile__("vmsr fpscr, %0" ::"r"(static_cast<uint32_t>(saved_)));
#endif
  }

  ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
  ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

 private:
  uint64_t saved_ = 0;
};

}  // namespace tonecore
