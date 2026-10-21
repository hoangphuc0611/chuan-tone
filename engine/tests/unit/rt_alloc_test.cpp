// Kiểm tra nguyên tắc real-time (CLAUDE.md 0.4): audio thread KHÔNG cấp phát bộ nhớ.
//
// Thay operator new/delete toàn cục bằng bản có đếm; chỉ đếm khi cờ thread_local bật, tức là
// chỉ trong lúc gọi hàm xử lý audio. Bỏ qua khi chạy với sanitizer (sanitizer tự thay operator
// new).

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include "tonecore/tonecore.h"
#include "util/synth_voice.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define TC_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define TC_SANITIZED 1
#endif

namespace {
thread_local bool g_counting = false;
std::atomic<int> g_allocs{0};
}  // namespace

#ifndef TC_SANITIZED
void* operator new(std::size_t size) {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (g_counting) g_allocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}
void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete[](void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
  std::free(p);
}
#endif

TEST(RealTime, ProcessDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  // Tự kiểm: bộ đếm phải bắt được một lần cấp phát thật.
  g_allocs.store(0);
  g_counting = true;
  auto* probe = new std::vector<int>(16);
  g_counting = false;
  delete probe;
  ASSERT_GE(g_allocs.load(), 1);

  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_set_key(e, 0, TC_SCALE_MAJOR), TC_OK);
  ASSERT_EQ(tc_set_correction_preset(e, TC_PRESET_SINGER), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);

  // Giọng có cả đoạn hữu thanh / vô thanh / đổi nốt để đi qua mọi nhánh.
  const auto s = tonecore::test::synthVoice(
      tonecore::test::vietTone(tonecore::test::VietTone::Nga, 200.0), 2.0);
  std::vector<float> out(2 * 1024);
  // Thử nhiều cỡ block, kể cả lớn hơn hop và lẻ.
  const uint32_t blocks[] = {64, 128, 192, 256, 333, 1024};
  std::size_t off = 0;
  g_allocs.store(0);
  for (int round = 0; off + 1024 <= s.audio.size(); ++round) {
    const uint32_t n = blocks[round % 6];
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, s.audio.data() + off, out.data(), n);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    off += n;
    if (round % 7 == 0) {  // đổi tham số giữa chừng (luồng điều khiển – được phép cấp phát)
      tc_set_correction_preset(e, round % 2 == 0 ? TC_PRESET_ROBOT : TC_PRESET_NATURAL);
    }
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
#endif
}
