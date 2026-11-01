// Thu âm đa track (M7, docs/m7-plan.md): WAV, bộ thu (ring + luồng ghi), C API tc_record_*.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "record/recorder.h"
#include "record/wav_file.h"
#include "test_util.h"
#include "tonecore/tonecore.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

std::string makeDir(const std::string& name) {
  const std::string d = tonecore::test::tempPath(name);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

tonecore::WavData read(const std::string& path) {
  tonecore::WavData w;
  EXPECT_EQ(tonecore::readWav(path, w), TC_OK) << path;
  return w;
}

tc_record_status status(tc_engine* e) {
  tc_record_status s{};
  s.struct_size = sizeof(s);
  EXPECT_EQ(tc_record_get_status(e, &s), TC_OK);
  return s;
}

struct Engine {
  tc_engine* e = nullptr;
  Engine() {
    EXPECT_EQ(tc_engine_create(nullptr, &e), TC_OK);
    EXPECT_EQ(tc_engine_start(e), TC_OK);
  }
  ~Engine() { tc_engine_destroy(e); }

  int32_t load(const std::string& path) {
    EXPECT_EQ(tc_load_beat(e, path.c_str()), TC_OK);
    tc_event buf[32];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
      const int32_t n = tc_poll_events(e, buf, 32);
      for (int32_t k = 0; k < n; ++k) {
        if (buf[k].type == TC_EVENT_BEAT_LOADED) return buf[k].code;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -999;
  }
};

// Micro giả: sin 220 Hz, nối tiếp qua các block.
std::vector<float> micSignal(std::size_t frames) {
  std::vector<float> v(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    v[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / kRate));
  }
  return v;
}

}  // namespace

TEST(Wav, RoundTripFloatExactInt16WithinOneStep) {
  const std::string dir = makeDir("wav_rt");
  std::vector<float> x(3 * 1000);
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = static_cast<float>(1.3 * std::sin(0.01 * static_cast<double>(i)));  // có mẫu > 1
  }
  tonecore::WavWriter w;
  ASSERT_EQ(w.open(dir + "/f.wav", 3, 44100, tonecore::WavFormat::Float32), TC_OK);
  ASSERT_TRUE(w.write(x.data(), 600));
  ASSERT_TRUE(w.write(x.data() + 3 * 600, 400));
  EXPECT_TRUE(fs::exists(dir + "/f.wav.part"));
  EXPECT_FALSE(fs::exists(dir + "/f.wav"));
  ASSERT_TRUE(w.finish());
  EXPECT_FALSE(fs::exists(dir + "/f.wav.part"));
  const auto f = read(dir + "/f.wav");
  EXPECT_EQ(f.sampleRate, 44100u);
  EXPECT_EQ(f.channels, 3u);
  ASSERT_EQ(f.frames, 1000u);
  EXPECT_EQ(f.samples, x);  // float giữ nguyên từng bit

  ASSERT_EQ(w.open(dir + "/i.wav", 3, 44100, tonecore::WavFormat::Int16), TC_OK);
  ASSERT_TRUE(w.write(x.data(), 1000));
  ASSERT_TRUE(w.finish());
  const auto s = read(dir + "/i.wav");
  ASSERT_EQ(s.frames, 1000u);
  for (std::size_t i = 0; i < x.size(); ++i) {
    const float want = std::fmax(-1.0f, std::fmin(1.0f, x[i]));
    ASSERT_NEAR(s.samples[i], want, 2.0 / 32768.0) << i;  // kẹp ±1, lượng tử 16 bit
  }

  ASSERT_EQ(w.open(dir + "/x.wav", 1, 48000, tonecore::WavFormat::Int16), TC_OK);
  w.abort();  // huỷ: không để lại gì
  EXPECT_FALSE(fs::exists(dir + "/x.wav.part"));
  EXPECT_FALSE(fs::exists(dir + "/x.wav"));
  fs::remove_all(dir);
}

// Ba file khớp đúng tín hiệu: giọng khô = micro (từng bit), beat = file beat từ vị trí bắt đầu,
// đầu ra = (beat + giọng đã xử lý) chậm output_lookahead_frames.
TEST(RecordApi, ThreeTracksMatchSignals) {
  Engine en;
  tc_engine* e = en.e;
  const std::string beatPath = tonecore::test::tempPath("rec_beat.wav");
  ASSERT_TRUE(tonecore::test::writeWav16(
      beatPath, tonecore::test::sineInt16(440.0, kRate, kRate * 3, 2, 0.25), kRate, 2));
  ASSERT_EQ(en.load(beatPath), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_MONITOR, TC_PARAM_ENABLED, 1.0f), TC_OK);
  ASSERT_EQ(tc_set_param(e, TC_NODE_FX, TC_PARAM_ENABLED, 0.0f), TC_OK);  // "Khô"
  ASSERT_EQ(tc_seek(e, 500.0), TC_OK);
  ASSERT_EQ(tc_play(e), TC_OK);

  const std::string dir = makeDir("rec_three");
  ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
  EXPECT_EQ(status(e).state, TC_RECORD_RECORDING);
  constexpr int kBlocks = 200;  // ~1,07 s
  const auto mic = micSignal(kBlocks * kBlock);
  std::vector<float> out(2 * kBlock * kBlocks);
  for (int b = 0; b < kBlocks; ++b) {
    ASSERT_EQ(
        tc_engine_process_offline(e, mic.data() + b * kBlock, out.data() + 2 * b * kBlock, kBlock),
        TC_OK);
  }
  ASSERT_EQ(tc_record_stop(e), TC_OK);
  const tc_record_status s = status(e);
  EXPECT_EQ(s.state, TC_RECORD_DONE);
  EXPECT_EQ(s.sample_rate, kRate);
  EXPECT_EQ(s.frames, static_cast<uint64_t>(kBlocks * kBlock));
  EXPECT_EQ(s.dropped_frames, 0u);
  EXPECT_EQ(s.start_beat_frame, kRate / 2);
  EXPECT_EQ(s.start_host_time_ns, 0);         // backend NULL không có đồng hồ máy
  EXPECT_EQ(s.output_lookahead_frames, 48u);  // limiter 1 ms
  EXPECT_EQ(s.pitch_latency_frames, 0u);      // chỉnh giọng mặc định tắt

  const auto dry = read(dir + "/dry.wav");
  const auto wet = read(dir + "/wet.wav");
  const auto beat = read(dir + "/beat.wav");
  ASSERT_EQ(dry.channels, 1u);
  ASSERT_EQ(wet.channels, 2u);
  ASSERT_EQ(beat.channels, 2u);
  ASSERT_EQ(dry.frames, s.frames);
  ASSERT_EQ(wet.frames, s.frames);
  ASSERT_EQ(beat.frames, s.frames);
  EXPECT_EQ(dry.samples, mic);

  // Beat = file beat từ 0,5 s (int16 → float → int16: lệch ≤ 1 bậc), sau đoạn fade-in lúc bấm
  // phát.
  for (std::size_t i = kRate / 20; i < beat.frames; ++i) {
    const double want =
        0.25 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i + kRate / 2) / kRate);
    ASSERT_NEAR(beat.samples[2 * i], want, 3.0 / 32768.0) << i;
  }

  // Giọng đã xử lý ("Khô"): hai kênh như nhau, cỡ như micro (chỉ qua lọc thông cao 80 Hz).
  double eDry = 0.0;
  double eWet = 0.0;
  for (std::size_t i = kRate / 10; i < wet.frames; ++i) {
    ASSERT_EQ(wet.samples[2 * i], wet.samples[2 * i + 1]);
    eDry += static_cast<double>(dry.samples[i]) * dry.samples[i];
    eWet += static_cast<double>(wet.samples[2 * i]) * wet.samples[2 * i];
  }
  EXPECT_NEAR(std::sqrt(eWet / eDry), 1.0, 0.1);

  // Đầu ra ở frame n + L = beat[n] + giọng đã xử lý[n] (nghe lại gain 1, dưới trần limiter).
  const std::size_t L = s.output_lookahead_frames;
  for (std::size_t n = kRate / 10; n + L < s.frames; ++n) {
    for (int c = 0; c < 2; ++c) {
      const double want = beat.samples[2 * n + c] + wet.samples[2 * n + c];
      ASSERT_NEAR(out[2 * (n + L) + c], want, 4.0 / 32768.0) << n;
    }
  }
  fs::remove_all(dir);
  std::remove(beatPath.c_str());
}

TEST(RecordApi, MaxSecondsStopsByItself) {
  Engine en;
  const std::string dir = makeDir("rec_max");
  tc_record_config cfg;
  tc_record_config_init(&cfg);
  cfg.max_seconds = 0.25f;
  ASSERT_EQ(tc_record_start(en.e, dir.c_str(), &cfg), TC_OK);
  std::vector<float> out(2 * kBlock);
  for (int b = 0; b < 100; ++b) {  // 0,53 s
    ASSERT_EQ(tc_engine_process_offline(en.e, nullptr, out.data(), kBlock), TC_OK);
  }
  tc_record_status s{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    s = status(en.e);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  } while (s.state != TC_RECORD_DONE && std::chrono::steady_clock::now() < deadline);
  EXPECT_EQ(s.state, TC_RECORD_DONE);
  EXPECT_EQ(s.frames, kRate / 4);
  EXPECT_EQ(read(dir + "/beat.wav").frames, kRate / 4);
  EXPECT_EQ(tc_record_stop(en.e), TC_OK);  // đã tự dừng: stop chỉ báo kết quả
  // Thu lần nữa vào cùng thư mục: ghi đè.
  ASSERT_EQ(tc_record_start(en.e, dir.c_str(), nullptr), TC_OK);
  for (int b = 0; b < 10; ++b) {
    ASSERT_EQ(tc_engine_process_offline(en.e, nullptr, out.data(), kBlock), TC_OK);
  }
  ASSERT_EQ(tc_record_stop(en.e), TC_OK);
  EXPECT_EQ(read(dir + "/dry.wav").frames, 10u * kBlock);
  fs::remove_all(dir);
}

TEST(RecordApi, EngineStopFinalizesFiles) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  const std::string dir = makeDir("rec_stop");
  ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
  std::vector<float> out(2 * kBlock);
  for (int b = 0; b < 20; ++b) {
    ASSERT_EQ(tc_engine_process_offline(e, nullptr, out.data(), kBlock), TC_OK);
  }
  ASSERT_EQ(tc_engine_stop(e), TC_OK);
  EXPECT_EQ(status(e).state, TC_RECORD_DONE);
  EXPECT_EQ(read(dir + "/wet.wav").frames, 20u * kBlock);

  // Huỷ engine giữa lúc thu cũng chốt file.
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
  for (int b = 0; b < 5; ++b) {
    ASSERT_EQ(tc_engine_process_offline(e, nullptr, out.data(), kBlock), TC_OK);
  }
  tc_engine_destroy(e);
  EXPECT_EQ(read(dir + "/dry.wav").frames, 5u * kBlock);
  fs::remove_all(dir);
}

TEST(RecordApi, RejectsBadCalls) {
  tc_engine* e = nullptr;
  ASSERT_EQ(tc_engine_create(nullptr, &e), TC_OK);
  const std::string dir = makeDir("rec_err");
  EXPECT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_ERR_INVALID_STATE);  // chưa start
  ASSERT_EQ(tc_engine_start(e), TC_OK);
  EXPECT_EQ(tc_record_stop(e), TC_ERR_INVALID_STATE);  // chưa từng thu
  EXPECT_EQ(tc_record_start(e, nullptr, nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_record_start(e, "", nullptr), TC_ERR_INVALID_ARG);
  EXPECT_EQ(tc_record_start(e, (dir + "/khong-co").c_str(), nullptr), TC_ERR_INVALID_ARG);
  tc_record_config cfg;
  tc_record_config_init(&cfg);
  cfg.max_seconds = -1.0f;
  EXPECT_EQ(tc_record_start(e, dir.c_str(), &cfg), TC_ERR_INVALID_ARG);
  cfg.max_seconds = 0.0f;
  cfg.struct_size = 4;
  EXPECT_EQ(tc_record_start(e, dir.c_str(), &cfg), TC_ERR_INVALID_ARG);
  tc_record_status st{};
  st.struct_size = 8;
  EXPECT_EQ(tc_record_get_status(e, &st), TC_ERR_INVALID_ARG);
  EXPECT_EQ(status(e).state, TC_RECORD_IDLE);

  ASSERT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_OK);
  EXPECT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_ERR_INVALID_STATE);  // đang thu
  EXPECT_EQ(tc_measure_latency_start(e), TC_ERR_INVALID_STATE);  // đang thu thì không đo
  ASSERT_EQ(tc_record_stop(e), TC_OK);
  // Đang đo độ trễ thì không thu.
  ASSERT_EQ(tc_measure_latency_start(e), TC_OK);
  EXPECT_EQ(tc_record_start(e, dir.c_str(), nullptr), TC_ERR_INVALID_STATE);
  tc_engine_destroy(e);
  fs::remove_all(dir);
}

// Ring đầy (luồng ghi tạm dừng): bỏ nguyên khối, đếm frame rơi, phần đã vào ring vẫn ghi đủ.
TEST(Recorder, FullRingDropsWholeBlocks) {
  const std::string dir = makeDir("rec_ring");
  // 0,05 s = 2400 frame × 5 kênh, ring làm tròn lên 16384 float = 3276 frame → vừa 12 khối 256.
  tonecore::Recorder rec(kRate, kBlock, 0.05);
  ASSERT_EQ(rec.start(dir, 0.0, {}), TC_OK);
  rec.pauseWriterForTest(true);
  std::vector<float> dry(kBlock);
  std::vector<float> wl(kBlock, 0.1f);
  std::vector<float> wr(kBlock, -0.1f);
  std::vector<float> beat(2 * kBlock, 0.2f);
  for (int b = 0; b < 20; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i) dry[i] = static_cast<float>(b * kBlock + i);
    rec.capture(dry.data(), wl.data(), wr.data(), beat.data(), kBlock, 1000, 77);
  }
  rec.pauseWriterForTest(false);
  ASSERT_EQ(rec.stop(true), TC_OK);
  tc_record_status s{};
  rec.fillStatus(s);
  EXPECT_EQ(s.state, TC_RECORD_DONE);
  EXPECT_EQ(s.frames, 12u * kBlock);
  EXPECT_EQ(s.dropped_frames, 8u * kBlock);
  EXPECT_EQ(s.start_beat_frame, 1000u);
  EXPECT_EQ(s.start_host_time_ns, 77);
  const auto d = read(dir + "/dry.wav");
  ASSERT_EQ(d.frames, 12u * kBlock);
  for (std::size_t i = 0; i < d.frames; ++i) ASSERT_EQ(d.samples[i], static_cast<float>(i));
  const auto w = read(dir + "/wet.wav");
  EXPECT_NEAR(w.samples[0], 0.1, 1e-4);
  EXPECT_NEAR(w.samples[1], -0.1, 1e-4);
  fs::remove_all(dir);
}
