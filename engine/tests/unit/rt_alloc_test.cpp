// Kiểm tra nguyên tắc real-time (CLAUDE.md 0.4): audio thread KHÔNG cấp phát bộ nhớ.
//
// Thay operator new/delete toàn cục bằng bản có đếm; chỉ đếm khi cờ thread_local bật, tức là
// chỉ trong lúc gọi hàm xử lý audio. Bỏ qua khi chạy với sanitizer (sanitizer tự thay operator
// new).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <thread>
#include <vector>

#include "codec/flac_encoder.h"
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

// Beat có stem giọng (M6): phát phần nhạc + k · giọng, đổi k và đổi tone (render cả hai) khi đang
// phát — audio thread không cấp phát.
TEST(RealTime, VocalStemDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const auto vocal = tonecore::test::sineInt16(880.0, 44100, 44100 * 4, 2, 0.2);
  auto mix = tonecore::test::sineInt16(330.0, 44100, 44100 * 4, 2, 0.2);
  for (std::size_t i = 0; i < mix.size(); ++i) {
    mix[i] = static_cast<int16_t>(mix[i] + vocal[i]);
  }
  const std::string path = tonecore::test::tempPath("rt_stem_mix.wav");
  const std::string vpath = tonecore::test::tempPath("rt_stem_vocal.flac");
  ASSERT_TRUE(tonecore::test::writeWav16(path, mix, 44100, 2));
  ASSERT_EQ(tonecore::writeFileAtomic(
                vpath, tonecore::encodeFlac(vocal.data(), vocal.size() / 2, 2, 44100)),
            TC_OK);
  const char* vocals[] = {vpath.c_str()};
  ASSERT_EQ(tc_load_beat_with_vocals(e, path.c_str(), vocals, 1), TC_OK);
  bool loaded = false;
  for (int i = 0; i < 5000 && !loaded; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    for (int32_t k = 0; k < n; ++k) loaded = loaded || ev[k].type == TC_EVENT_BEAT_LOADED;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_TRUE(loaded);
  ASSERT_EQ(tc_play(e), TC_OK);
  std::vector<float> out(2 * 512);
  g_allocs.store(0);
  for (int round = 0; round < 300; ++round) {
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, nullptr, out.data(), 512);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    if (round % 20 == 5) {
      ASSERT_EQ(tc_set_param(e, TC_NODE_VOCAL, TC_PARAM_GAIN, (round / 20) % 2 ? 0.0f : 0.8f),
                TC_OK);
    }
    if (round == 50) ASSERT_EQ(tc_set_transpose(e, 2), TC_OK);
    if (round == 200) ASSERT_EQ(tc_set_transpose(e, 0), TC_OK);
    if (round % 4 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tc_event ev[8];
    tc_poll_events(e, ev, 8);
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
  std::remove(path.c_str());
  std::remove(vpath.c_str());
#endif
}

// Thu âm (M7): audio thread chỉ chép vào ring cấp sẵn — kể cả lúc bắt đầu, lúc dừng và khi
// ring đầy.
TEST(RealTime, RecordingDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const std::string path = tonecore::test::tempPath("rt_rec_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(
      path, tonecore::test::sineInt16(330.0, 48000, 48000 * 4, 2, 0.2), 48000, 2));
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
  const std::string dir = tonecore::test::tempPath("rt_rec_dir");
  std::filesystem::create_directories(dir);
  std::vector<float> in(512, 0.1f);
  std::vector<float> out(2 * 512);
  g_allocs.store(0);
  for (int take = 0; take < 2; ++take) {
    ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
    for (int round = 0; round < 150; ++round) {
      g_counting = true;
      const tc_result r = tc_engine_process_offline(e, in.data(), out.data(), 512);
      g_counting = false;
      ASSERT_EQ(r, TC_OK);
    }
    // Dừng từ luồng khác trong lúc audio vẫn chạy (xác nhận ở block kế tiếp).
    std::thread stopper([e] { EXPECT_EQ(tc_record_stop(e), TC_OK); });
    for (int round = 0; round < 50; ++round) {
      g_counting = true;
      tc_engine_process_offline(e, in.data(), out.data(), 512);
      g_counting = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stopper.join();
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
  std::filesystem::remove_all(dir);
  std::remove(path.c_str());
#endif
}

// Nghe lại bản thu (M7): giọng bản thu thay micro, đổi preset / chỉnh giọng khi đang phát.
TEST(RealTime, TakeReviewDoesNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const std::string dir = tonecore::test::tempPath("rt_take_dir");
  std::filesystem::create_directories(dir);
  std::vector<float> in(512, 0.1f);
  std::vector<float> out(2 * 512);
  ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
  for (int round = 0; round < 200; ++round)
    tc_engine_process_offline(e, in.data(), out.data(), 512);
  ASSERT_EQ(tc_record_stop(e), TC_OK);
  tc_take_config cfg;
  tc_take_config_init(&cfg);
  cfg.voice_offset_frames = 600;
  ASSERT_EQ(tc_load_take(e, (dir + "/beat.wav").c_str(), (dir + "/dry.wav").c_str(), &cfg), TC_OK);
  bool loaded = false;
  for (int i = 0; i < 5000 && !loaded; ++i) {
    tc_event ev[8];
    const int32_t n = tc_poll_events(e, ev, 8);
    for (int32_t k = 0; k < n; ++k) loaded = loaded || ev[k].type == TC_EVENT_BEAT_LOADED;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_TRUE(loaded);
  ASSERT_EQ(tc_play(e), TC_OK);
  tc_fx_preset bolero;
  tc_fx_preset_init(&bolero, TC_GENRE_BOLERO);
  tc_fx_preset pop;
  tc_fx_preset_init(&pop, TC_GENRE_POP);
  g_allocs.store(0);
  for (int round = 0; round < 180; ++round) {
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, in.data(), out.data(), 512);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    if (round == 30) ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
    if (round == 60) ASSERT_EQ(tc_apply_fx_preset(e, &bolero), TC_OK);
    if (round == 120) ASSERT_EQ(tc_apply_fx_preset(e, &pop), TC_OK);
    if (round == 150) ASSERT_EQ(tc_seek(e, 100.0), TC_OK);
    if (round % 4 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    tc_event ev[8];
    tc_poll_events(e, ev, 8);
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
  std::filesystem::remove_all(dir);
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

// Engine: đổi preset FX / "Vang" / gate giữa các block (luồng điều khiển — được phép cấp phát; đổi
// cấu trúc → dựng chuỗi mới ở đây). Audio thread nhận chuỗi, crossfade, cho chuỗi cũ ngân rồi trả
// về mà không cấp phát.
TEST(RealTime, FxPresetSwapsDoNotAllocate) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_GATE, TC_PARAM_ENABLED, 1.0f), TC_OK);
  const auto s = tonecore::test::synthVoice(
      tonecore::test::vietTone(tonecore::test::VietTone::Sac, 220.0), 3.0);
  std::vector<float> out(2 * 512);
  g_allocs.store(0);
  std::size_t off = 0;
  for (int round = 0; off + 512 <= s.audio.size(); ++round) {
    if (round % 40 == 0) {
      tc_fx_preset p;
      tc_fx_preset_init(&p, 1 + (round / 40) % 6);
      tc_apply_fx_preset(e, &p);
      tc_set_param(e, TC_NODE_FX, TC_PARAM_MIX, 0.5f + 0.1f * static_cast<float>(round % 10));
    }
    if (round % 25 == 0) {
      tc_event ev[8];
      tc_poll_events(e, ev, 8);  // dọn chuỗi cũ (luồng điều khiển)
    }
    const uint32_t n = round % 2 == 0 ? 512u : 333u;
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, s.audio.data() + off, out.data(), n);
    g_counting = false;
    ASSERT_EQ(r, TC_OK);
    off += n;
  }
  EXPECT_EQ(g_allocs.load(), 0);
  tc_engine_destroy(e);
#endif
}

// "Hát 30 phút" (M9 B3, CLAUDE.md mục 7: không xrun / rè trong 30 phút hát liên tục) — phần DSP:
// giọng giả (thanh điệu, rung, lướt nốt, khoảng lặng có hơi thở / tiếng "s") + beat to, bật chỉnh
// giọng; trong lúc hát đổi thể loại mỗi 30 s, mức chỉnh giọng mỗi phút (có lúc tắt), tone beat mỗi
// 2 phút, tua mỗi 90 s, hết bài thì phát lại. Kiểm: không NaN / Inf, không vượt trần limiter (<
// 1%), không cấp phát trong audio callback, thời gian xử lý mỗi khối không tăng dần (trung vị đoạn
// cuối so với đoạn đầu). Số phút: biến môi trường TC_SOAK_MINUTES (mặc định 1 cho mọi lần chạy
// test; CI chạy 30 phút ở bản Release — .github/workflows/engine.yml, job bench).
TEST(RealTime, LongSessionSoak) {
#ifdef TC_SANITIZED
  GTEST_SKIP() << "Sanitizer tự thay operator new – chạy ở bản build thường.";
#else
  namespace t = tonecore::test;
  double minutes = 1.0;
  if (const char* m = std::getenv("TC_SOAK_MINUTES")) minutes = std::max(0.2, std::atof(m));
  constexpr uint32_t kRate = 48000;
  constexpr uint32_t kBlock = 256;

  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_GATE, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_key(e, 9, TC_SCALE_NATURAL_MINOR), TC_OK);

  // Beat 75 s: hợp âm La thứ to (để limiter phải làm việc khi giọng to chồng lên).
  const std::string path = t::tempPath("rt_soak_beat.wav");
  {
    const std::size_t frames = 75u * kRate;
    std::vector<int16_t> beat(2 * frames);
    const double hz[] = {110.0, 220.0, 261.63, 329.63, 440.0};
    for (std::size_t i = 0; i < frames; ++i) {
      double v = 0.0;
      for (double f : hz)
        v += std::sin(2.0 * 3.14159265358979 * f * static_cast<double>(i) / kRate);
      const double beatEnv =
          0.6 +
          0.4 * std::fabs(std::sin(3.14159265358979 * static_cast<double>(i) / (kRate / 2.0)));
      const auto s = static_cast<int16_t>(std::lround(32767.0 * 0.16 * beatEnv * v));
      beat[2 * i] = s;
      beat[2 * i + 1] = s;
    }
    ASSERT_TRUE(t::writeWav16(path, beat, kRate, 2));
  }
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

  // Kho câu hát ~20 s lặp lại: các thanh, rung, lướt, câu to, khoảng lặng có hơi thở / "s".
  std::vector<float> voice;
  auto append = [&](const std::vector<float>& a, float gain) {
    for (float v : a) voice.push_back(v * gain);
  };
  const t::VietTone tones[] = {t::VietTone::Ngang, t::VietTone::Huyen, t::VietTone::Sac,
                               t::VietTone::Hoi,   t::VietTone::Nga,   t::VietTone::Nang};
  for (int i = 0; i < 6; ++i) {
    t::VoiceSpec spec;
    spec.seed = 11 + i;
    spec.vowel = static_cast<t::Vowel>(i % 3);
    append(t::synthVoice(t::vietTone(tones[i], 180.0 + 25.0 * i), 1.6, spec).audio, 1.0f);
    append(t::whiteNoise(0.25, kRate, 0.004, 7 + i), 1.0f);
  }
  append(t::synthVoice(t::vibrato(330.0, 5.5, 60.0), 3.0).audio, 2.4f);  // câu ngân to
  append(t::fricativeS(0.3, kRate, 0.05), 1.0f);
  append(t::synthVoice(t::glide(196.0, 392.0, 0.2, 1.2), 2.0).audio, 1.0f);
  append(std::vector<float>(kRate, 0.0f), 1.0f);                     // lặng 1 s
  append(t::synthVoice(t::steady(261.63 * 1.03), 2.5).audio, 1.8f);  // lệch tone → chỉnh nhiều

  const auto totalBlocks = static_cast<std::size_t>(minutes * 60.0 * kRate / kBlock);
  std::vector<float> micros(totalBlocks);
  std::vector<float> out(2 * kBlock);
  std::size_t off = 0;
  int bad = 0;
  float peak = 0.0f;
  int restarts = 0;
  const int32_t transposes[] = {2, -3, 4, 0, -1};
  const int32_t corrections[] = {TC_PRESET_NATURAL, TC_PRESET_SINGER, TC_PRESET_ROBOT, -1};
  g_allocs.store(0);
  for (std::size_t b = 0; b < totalBlocks; ++b) {
    const std::size_t sample = b * kBlock;
    // Luồng điều khiển (được phép cấp phát) — theo giây âm thanh, như người dùng bấm.
    if (sample % (30 * kRate) < kBlock) {
      tc_fx_preset p;
      tc_fx_preset_init(&p, static_cast<int32_t>((sample / (30 * kRate)) % 7));
      ASSERT_EQ(tc_apply_fx_preset(e, &p), TC_OK);
    }
    if ((sample + 10 * kRate) % (60 * kRate) < kBlock) {
      const int32_t c = corrections[(sample / (60 * kRate)) % 4];
      ASSERT_EQ(tc_set_param(e, TC_NODE_PITCH, TC_PARAM_ENABLED, c < 0 ? 0.0f : 1.0f), TC_OK);
      if (c >= 0) ASSERT_EQ(tc_set_correction_preset(e, c), TC_OK);
    }
    if ((sample + 45 * kRate) % (120 * kRate) < kBlock) {
      ASSERT_EQ(tc_set_transpose(e, transposes[(sample / (120 * kRate)) % 5]), TC_OK);
    }
    if ((sample + 20 * kRate) % (90 * kRate) < kBlock) {
      ASSERT_EQ(tc_seek(e, 1000.0 * static_cast<double>((sample / kRate) % 70)), TC_OK);
    }
    if (b % 20 == 0) {
      tc_event ev[8];
      const int32_t n = tc_poll_events(e, ev, 8);
      for (int32_t k = 0; k < n; ++k) {
        if (ev[k].type == TC_EVENT_BEAT_ENDED) {
          ++restarts;
          ASSERT_EQ(tc_seek(e, 0.0), TC_OK);
          ASSERT_EQ(tc_play(e), TC_OK);
        }
      }
    }
    if (off + kBlock > voice.size()) off = 0;
    const auto t0 = std::chrono::steady_clock::now();
    g_counting = true;
    const tc_result r = tc_engine_process_offline(e, voice.data() + off, out.data(), kBlock);
    g_counting = false;
    const auto t1 = std::chrono::steady_clock::now();
    ASSERT_EQ(r, TC_OK);
    micros[b] = std::chrono::duration<float, std::micro>(t1 - t0).count();
    off += kBlock;
    for (float v : out) {
      // Limiter đầu ra: trần 0,98, cho phép vượt < 1% (src/fx/peak_limiter.h) → luôn < 0,99.
      if (!std::isfinite(v) || std::fabs(v) > 0.99f) ++bad;
      peak = std::max(peak, std::fabs(v));
    }
  }
  EXPECT_EQ(g_allocs.load(), 0);
  EXPECT_EQ(bad, 0) << "mẫu NaN / Inf / vượt trần";
  EXPECT_GT(peak, 0.3f) << "đầu ra im lặng bất thường";

  // Thời gian mỗi khối: trung vị 1/6 cuối so với 1/6 đầu (denormal / rò rỉ làm chậm dần → tăng
  // nhiều lần; nhiễu máy CI chỉ vài chục %).
  auto median = [&](std::size_t from, std::size_t to) {
    std::vector<float> v(micros.begin() + static_cast<std::ptrdiff_t>(from),
                         micros.begin() + static_cast<std::ptrdiff_t>(to));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
  };
  const std::size_t sixth = totalBlocks / 6;
  const float first = median(0, sixth);
  const float last = median(totalBlocks - sixth, totalBlocks);
  std::vector<float> sorted = micros;
  std::sort(sorted.begin(), sorted.end());
  const float p99 = sorted[sorted.size() * 99 / 100];
  double sum = 0.0;
  for (float m : micros) sum += m;
  const double blockUs = 1e6 * kBlock / kRate;
  std::printf(
      "[soak] %.1f phút, %zu khối: trung bình %.1f µs (%.2f%% một nhân), p99 %.1f µs (%.2f%%), "
      "trung vị đầu %.1f / cuối %.1f µs, đỉnh %.3f, phát lại %d lần\n",
      minutes, totalBlocks, sum / static_cast<double>(totalBlocks),
      100.0 * sum / static_cast<double>(totalBlocks) / blockUs, p99, 100.0 * p99 / blockUs, first,
      last, peak, restarts);
  EXPECT_LE(last, 1.5f * first + 2.0f) << "xử lý chậm dần theo thời gian";

  tc_playback_state st{};
  st.struct_size = sizeof(st);
  ASSERT_EQ(tc_get_playback_state(e, &st), TC_OK);
  EXPECT_GE(st.position_ms, 0.0);
  EXPECT_LE(st.position_ms, st.duration_ms);
  tc_engine_destroy(e);
  std::remove(path.c_str());
#endif
}
