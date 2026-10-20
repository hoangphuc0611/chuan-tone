#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace tonecore {

// Đo độ trễ round-trip (loa → micro hoặc qua dongle loopback) bằng chuỗi MLS.
//
// Cách làm:
//  1. Sau một đoạn im lặng (preroll) cho đường âm thanh ổn định, phát `repeats` lần chuỗi MLS,
//     mỗi lần cách nhau P = độ dài MLS + độ trễ tối đa → tiếng vọng của lần trước đã tắt hẳn
//     trước khi cửa sổ tìm của lần sau bắt đầu.
//  2. Đồng thời ghi lại toàn bộ tín hiệu micro vào buffer cấp phát sẵn.
//  3. Ở luồng nền: với mỗi lần phát, tính tương quan chéo giữa MLS và tín hiệu thu trong cửa
//     sổ độ trễ [0, maxLatency). Đỉnh |tương quan| là độ trễ (lấy trị tuyệt đối vì loa/mic
//     có thể đảo pha). Nội suy parabol quanh đỉnh để có độ chính xác dưới một mẫu.
//  4. Lấy trung vị các lần đo hợp lệ.
//
// Luồng: arm()/analyze() gọi từ luồng điều khiển/nền; processBlock() gọi từ audio thread.
class LatencyProbe {
 public:
  struct Config {
    int mlsOrder = 13;  // 8191 mẫu ≈ 170 ms ở 48 kHz
    int repeats = 5;
    float level = 0.5f;          // biên độ phát (−6 dBFS)
    double maxLatencySec = 0.5;  // cửa sổ tìm độ trễ
    double prerollSec = 0.2;     // im lặng trước lần phát đầu
  };

  struct BurstResult {
    bool valid = false;
    double latencyFrames = 0.0;
    float confidence = 0.0f;  // 1 − (đỉnh phụ / đỉnh chính), 0..1
  };

  struct Result {
    bool valid = false;
    double latencyFrames = 0.0;  // trung vị các lần hợp lệ
    double latencyMs = 0.0;
    float confidence = 0.0f;  // trung vị độ tin cậy
    int validBursts = 0;
    double spreadFrames = 0.0;  // chênh lệch lớn nhất giữa các lần hợp lệ
    std::vector<BurstResult> bursts;
  };

  LatencyProbe(uint32_t sampleRate, const Config& config);
  explicit LatencyProbe(uint32_t sampleRate) : LatencyProbe(sampleRate, Config{}) {}

  // Luồng điều khiển: bắt đầu một lần đo mới.
  void arm() noexcept;
  // Luồng điều khiển: huỷ lần đo đang chạy.
  void cancel() noexcept;

  // true khi đang chờ hoặc đang đo.
  bool isActive() const noexcept {
    const int s = state_.load(std::memory_order_acquire);
    return s == kArmed || s == kRunning;
  }
  bool isComplete() const noexcept { return state_.load(std::memory_order_acquire) == kComplete; }

  // [RT] Ghi tín hiệu micro (mono) và ghi tín hiệu kích thích (mono) vào `stimulusOut`.
  // Trả về true khi đang đo (engine nên tắt các nguồn âm khác để không gây nhiễu).
  bool processBlock(const float* micIn, float* stimulusOut, uint32_t frames) noexcept;

  // Luồng nền, sau khi isComplete(): phân tích dữ liệu đã ghi.
  // Không được gọi arm() lần nữa trong lúc analyze() đang chạy (engine đảm bảo điều này).
  Result analyze() const;

  uint32_t totalFrames() const noexcept { return totalFrames_; }
  uint32_t sampleRate() const noexcept { return sampleRate_; }

 private:
  enum State : int { kIdle = 0, kArmed = 1, kRunning = 2, kComplete = 3 };

  BurstResult analyzeBurst(int index) const;

  const uint32_t sampleRate_;
  const Config config_;
  const std::vector<float> mls_;
  const uint32_t maxLatencyFrames_;
  const uint32_t prerollFrames_;
  const uint32_t periodFrames_;
  const uint32_t totalFrames_;
  std::vector<float> capture_;
  uint32_t cursor_ = 0;  // chỉ audio thread đọc/ghi
  std::atomic<int> state_{kIdle};
};

}  // namespace tonecore
