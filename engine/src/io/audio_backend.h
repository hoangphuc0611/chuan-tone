#pragma once

#include <cstdint>
#include <memory>

#include "tonecore/tonecore.h"

namespace tonecore {

struct EngineConfig;

// Thông tin thiết bị âm thanh đang mở.
struct BackendInfo {
  uint32_t sampleRate = 0;
  uint32_t bufferFrames = 0;
  uint32_t burstFrames = 0;
  uint32_t inputChannels = 0;
  uint32_t outputChannels = 0;
  double inputLatencyMs = 0.0;   // hệ điều hành báo (ước lượng)
  double outputLatencyMs = 0.0;  // hệ điều hành báo (ước lượng)
  int32_t route = TC_ROUTE_UNKNOWN;
  int32_t api = TC_AUDIO_API_NONE;
};

// Phía engine mà backend gọi vào.
class BackendHost {
 public:
  virtual ~BackendHost() = default;
  // [RT] Gọi từ audio thread. in/out interleaved với số kênh thực tế của thiết bị.
  virtual void processAudio(const float* in, uint32_t inChannels, float* out, uint32_t outChannels,
                            uint32_t frames) noexcept = 0;
  // Gọi từ luồng của hệ điều hành (không phải audio thread).
  virtual void onRouteChanged(int32_t route) = 0;
  virtual void onInterruption(bool began) = 0;
  virtual void onStreamRestarted(tc_result result) = 0;
};

// Thiết bị âm thanh full-duplex (micro vào + loa/tai nghe ra cùng một callback).
class AudioBackend {
 public:
  virtual ~AudioBackend() = default;
  virtual tc_result start() = 0;  // mở + chạy stream
  virtual void stop() = 0;        // dừng + đóng stream; sau khi trả về, không còn callback nào
  virtual BackendInfo info() const = 0;
  virtual uint32_t xruns() const = 0;
};

// Tạo backend của nền tảng hiện tại (Oboe / RemoteIO). Trả nullptr nếu nền tảng chưa hỗ trợ
// (vd. bản host macOS/Linux dùng cho test).
std::unique_ptr<AudioBackend> createPlatformBackend(BackendHost& host, const EngineConfig& config);

}  // namespace tonecore
