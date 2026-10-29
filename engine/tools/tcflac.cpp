// tcflac — nén / giải mã FLAC bằng mã của engine, cho server tách giọng (M6) và đo trên máy dev.
//
//   tcflac decode in.flac out.f32            → PCM float32 interleaved (little-endian); in JSON
//                                              {"sample_rate":…,"channels":…,"frames":…}
//   tcflac encode RATE CHANNELS in.f32 out.flac   (float32 → 16-bit làm tròn + kẹp → FLAC)
//   tcflac export in.(m4a|mp3|wav|flac) out_prefix [RATE] [CHUNK_SECONDS]
//                                              → như app: giải mã + chia đoạn, in thời gian, tỉ lệ
//                                              nén
// Giải mã bằng dr_flac (Linux) / ExtAudioFile (macOS) — cùng decodeFile của engine.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "codec/flac_encoder.h"
#include "decode/decoder.h"
#include "tonecore/tonecore.h"

namespace {

int usage() {
  std::fprintf(stderr,
               "dùng: tcflac decode in.flac out.f32\n"
               "      tcflac encode RATE CHANNELS in.f32 out.flac\n"
               "      tcflac export in.m4a out_prefix [RATE] [CHUNK_SECONDS]\n");
  return 2;
}

int decode(const char* in, const char* out) {
  tonecore::DecodedAudio a;
  const tc_result r = tonecore::decodeFile(in, a);
  if (r != TC_OK) {
    std::fprintf(stderr, "tcflac: không giải mã được %s (%s)\n", in, tc_result_to_string(r));
    return 1;
  }
  std::vector<float> f(a.samples.size());
  for (std::size_t i = 0; i < f.size(); ++i) f[i] = static_cast<float>(a.samples[i]) / 32768.0f;
  std::FILE* o = std::fopen(out, "wb");
  if (o == nullptr || std::fwrite(f.data(), sizeof(float), f.size(), o) != f.size()) {
    std::fprintf(stderr, "tcflac: không ghi được %s\n", out);
    if (o != nullptr) std::fclose(o);
    return 1;
  }
  std::fclose(o);
  std::printf("{\"sample_rate\":%u,\"channels\":%u,\"frames\":%llu}\n", a.sampleRate, a.channels,
              static_cast<unsigned long long>(a.frames));
  return 0;
}

int encode(const char* rateArg, const char* channelsArg, const char* in, const char* out) {
  const auto rate = static_cast<uint32_t>(std::strtoul(rateArg, nullptr, 10));
  const auto channels = static_cast<uint32_t>(std::strtoul(channelsArg, nullptr, 10));
  std::FILE* f = std::fopen(in, "rb");
  if (f == nullptr) {
    std::fprintf(stderr, "tcflac: không mở được %s\n", in);
    return 1;
  }
  std::vector<float> x;
  float buf[4096];
  std::size_t n = 0;
  while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0) x.insert(x.end(), buf, buf + n);
  std::fclose(f);
  if (channels == 0 || x.size() % channels != 0) {
    std::fprintf(stderr, "tcflac: số mẫu không chia hết cho số kênh\n");
    return 1;
  }
  std::vector<int16_t> pcm(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) {
    const float v = std::round(x[i] * 32768.0f);
    pcm[i] = static_cast<int16_t>(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
  }
  try {
    const auto data = tonecore::encodeFlac(pcm.data(), pcm.size() / channels, channels, rate);
    if (tonecore::writeFileAtomic(out, data) != TC_OK) {
      std::fprintf(stderr, "tcflac: không ghi được %s\n", out);
      return 1;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "tcflac: %s\n", e.what());
    return 1;
  }
  return 0;
}

int exportFile(int argc, char** argv) {
  tc_export_config cfg;
  tc_export_config_init(&cfg);
  if (argc > 4) cfg.sample_rate = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
  if (argc > 5) cfg.chunk_seconds = std::strtof(argv[5], nullptr);
  tc_exporter* e = nullptr;
  if (tc_exporter_create(&e) != TC_OK) return 1;
  const auto t0 = std::chrono::steady_clock::now();
  if (tc_exporter_start(e, argv[2], argv[3], &cfg) != TC_OK) return 1;
  tc_export_status s{};
  s.struct_size = sizeof(s);
  double firstChunk = -1.0;
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    tc_exporter_get_status(e, &s);
    if (firstChunk < 0.0 && s.chunks_done > 0) {
      firstChunk = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
  } while (s.state == TC_ANALYSIS_RUNNING);
  const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  tc_exporter_destroy(e);
  if (s.state != TC_ANALYSIS_DONE) {
    std::fprintf(stderr, "tcflac: xuất lỗi (%s)\n", tc_result_to_string(s.result));
    return 1;
  }
  // Tổng kích thước các file đã ghi.
  uint64_t bytes = 0;
  for (uint32_t i = 0; i < s.chunks_total; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name),
                  s.chunks_total == 1 && !(cfg.chunk_seconds > 0.0f) ? ".flac" : "_%03u.flac", i);
    std::FILE* f = std::fopen((std::string(argv[3]) + name).c_str(), "rb");
    if (f == nullptr) continue;
    std::fseek(f, 0, SEEK_END);
    bytes += static_cast<uint64_t>(std::ftell(f));
    std::fclose(f);
  }
  const double pcmBytes = static_cast<double>(s.frames) * s.channels * 2.0;
  std::printf("%u đoạn, %.1f s âm thanh %u Hz %u kênh\n", s.chunks_total,
              static_cast<double>(s.frames) / s.sample_rate, s.sample_rate, s.channels);
  std::printf("thời gian %.2f s (đoạn đầu sau %.2f s)\n", total, firstChunk);
  std::printf("FLAC %.1f MB = %.1f%% PCM 16-bit\n", static_cast<double>(bytes) / 1e6,
              100.0 * static_cast<double>(bytes) / pcmBytes);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "decode" && argc == 4) return decode(argv[2], argv[3]);
  if (cmd == "encode" && argc == 6) return encode(argv[2], argv[3], argv[4], argv[5]);
  if (cmd == "export" && argc >= 4) return exportFile(argc, argv);
  return usage();
}
