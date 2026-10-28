// Kiểm tra nguyên tắc real-time (CLAUDE.md 0.4): audio thread KHÔNG cấp phát bộ nhớ.
//
// Thay operator new/delete toàn cục bằng bản có đếm; chỉ đếm khi cờ thread_local bật, tức là
// chỉ trong lúc gọi hàm xử lý audio. Bỏ qua khi chạy với sanitizer (sanitizer tự thay operator
// new).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

#include "fx/compressor.h"
#include "fx/de_esser.h"
#include "fx/delay.h"
#include "fx/equalizer.h"
#include "fx/noise_gate.h"
#include "fx/reverb.h"
#include "test_util.h"
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

#ifndef TC_SANITIZED
namespace {
thread_local bool g_counting = false;
std::atomic<int> g_allocs{0};
}  // namespace

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

// Timeline key (v0.4): audio thread đổi key theo vị trí beat, nhận timeline mới, tua — vẫn không
// cấp phát.
TEST(RealTime, KeyTimelineDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_tuning(e, 20.0f), TC_OK);

  // Beat 3 s (nạp ở luồng nền của engine — được phép cấp phát).
  const std::string path = tonecore::test::tempPath("rt_timeline_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(path, std::vector<int16_t>(48000 * 3 * 2, 0), 48000, 2));
  ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
  bool loaded = false;
  for (int i = 0; i < 5000 && !loaded; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    for (int32_t k = 0; k < n; ++k) loaded = loaded || ev[k].type == TC_EVENT_BEAT_LOADED;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_TRUE(loaded);
  ASSERT_EQ(tc_play(e), TC_OK);

  tc_key_segment tl[6];
  for (int i = 0; i < 6; ++i) {
    tl[i] = tc_key_segment{i * 500.0, i * 500.0 + 500.0, (i * 5) % 12,
                           i % 2 == 0 ? TC_SCALE_MAJOR : TC_SCALE_NATURAL_MINOR, 0.0f};
  }
  ASSERT_EQ(tc_set_key_timeline(e, tl, 6), TC_OK);

  const auto s = tonecore::test::synthVoice(
      tonecore::test::vietTone(tonecore::test::VietTone::Nga, 200.0), 2.0);
  std::vector<float> out(2 * 512);
  std::size_t off = 0;
  g_allocs.store(0);
  for (int round = 0; round < 250; ++round) {
    if (off + 512 > s.audio.size()) off = 0;
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, s.audio.data() + off, out.data(), 512);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    off += 512;
    // Luồng điều khiển: thay timeline, tua, lấy sự kiện (được phép cấp phát).
    if (round == 80) ASSERT_EQ(tc_set_key_timeline(e, tl + 1, 5), TC_OK);
    if (round == 120) ASSERT_EQ(tc_seek(e, 200.0), TC_OK);
    if (round % 10 == 0) {
      tc_event ev[8];
      tc_poll_events(e, ev, 8);
    }
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
  std::remove(path.c_str());
#endif
}

// Đổi tone beat (v0.5): audio thread nhận bản mới, chờ khối, crossfade, tua, về beat gốc — vẫn
// không cấp phát (render, tạo bản mới, huỷ bản cũ đều ở luồng nền).
TEST(RealTime, TransposeDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_key(e, 0, TC_SCALE_MAJOR), TC_OK);

  const std::string path = tonecore::test::tempPath("rt_transpose_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(
      path, tonecore::test::sineInt16(330.0, 48000, 48000 * 4, 2, 0.3), 48000, 2));
  ASSERT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
  bool loaded = false;
  for (int i = 0; i < 5000 && !loaded; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    for (int32_t k = 0; k < n; ++k) loaded = loaded || ev[k].type == TC_EVENT_BEAT_LOADED;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_TRUE(loaded);
  ASSERT_EQ(tc_play(e), TC_OK);

  const auto s = tonecore::test::synthVoice(
      tonecore::test::vietTone(tonecore::test::VietTone::Nga, 200.0), 2.0);
  std::vector<float> out(2 * 512);
  std::size_t off = 0;
  g_allocs.store(0);
  const int32_t tones[] = {3, -2, 5, 0, -6};
  for (int round = 0; round < 400; ++round) {
    if (off + 512 > s.audio.size()) off = 0;
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, s.audio.data() + off, out.data(), 512);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    off += 512;
    // Luồng điều khiển (được phép cấp phát): đổi tone, tua, lấy sự kiện. Chậm lại một chút để
    // luồng render kịp → audio thread đi qua cả nhánh chờ khối lẫn crossfade.
    if (round % 60 == 10) ASSERT_EQ(tc_set_transpose(e, tones[(round / 60) % 5]), TC_OK);
    if (round == 200) ASSERT_EQ(tc_seek(e, 2500.0), TC_OK);
    if (round % 4 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tc_event ev[8];
    tc_poll_events(e, ev, 8);
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
  std::remove(path.c_str());
#endif
}

// Khối FX giọng (M5): xử lý và đặt thông số (kể cả đang trượt) không cấp phát.
TEST(RealTime, FxBlocksDoNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tonecore::Equalizer eq(48000.0);
  tonecore::NoiseGate gate(48000.0);
  tonecore::Compressor comp(48000.0);
  tonecore::DeEsser deess(48000.0);
  tonecore::Delay delay(48000.0);
  tonecore::Reverb reverb(48000.0);
  std::vector<float> wet(1024), l(1024), r(1024);
  std::vector<float> x(1024);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * static_cast<float>(i % 37) / 37.0f;
  g_allocs.store(0);
  g_counting = true;
  for (int round = 0; round < 200; ++round) {
    tonecore::EqParams p;
    p.highPassHz = 80.0f;
    p.lowPassHz = round % 2 == 0 ? 9000.0f : 0.0f;
    p.bands[0] = {tonecore::SvfType::Bell, 300.0f + static_cast<float>(round), -2.0f, 1.0f};
    p.bands[1] = {tonecore::SvfType::HighShelf, 12000.0f, static_cast<float>(round % 5), 0.7f};
    eq.setParams(p);
    tonecore::GateParams g;
    g.thresholdDb = -50.0f + static_cast<float>(round % 10);
    gate.setParams(g);
    tonecore::CompressorParams cp;
    cp.thresholdDb = -30.0f + static_cast<float>(round % 20);
    comp.setParams(cp);
    tonecore::DeEsserParams dp;
    dp.thresholdDb = -60.0f + static_cast<float>(round % 30);  // có lúc giảm → tính lại shelf
    deess.setParams(dp);
    gate.process(x.data(), 333);
    eq.process(x.data(), 333);
    comp.process(x.data(), 333);
    deess.process(x.data(), 333);
    tonecore::DelayParams dl;
    dl.bpm = 80.0f + static_cast<float>(round % 40);
    dl.division = static_cast<tonecore::NoteDivision>(round % 6);
    delay.setParams(dl);
    tonecore::ReverbParams rp;
    rp.type = static_cast<tonecore::ReverbType>(round % 3);  // đổi kiểu: không cấp phát lại
    rp.decaySec = 0.6f + 0.01f * static_cast<float>(round);
    reverb.setParams(rp);
    delay.process(x.data(), wet.data(), 333);
    reverb.process(x.data(), l.data(), r.data(), 333);
  }
  g_counting = false;
  EXPECT_EQ(g_allocs.load(), 0);
#endif
}
