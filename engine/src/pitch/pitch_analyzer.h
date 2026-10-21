#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "dsp/decimator.h"
#include "pitch/f0_tracker.h"
#include "pitch/mpm.h"
#include "pitch/yin.h"

namespace tonecore {

enum class PitchAlgorithm { Mpm, Yin };

struct PitchAnalyzerConfig {
  double inputRate = 48000.0;
  double targetAnalysisRate = 16000.0;  // tần số phân tích thực = inputRate / round(input/target)
  double windowMs = 40.0;
  double hopMs = 4.0;
  double minF0 = 70.0;
  double maxF0 = 1100.0;
  PitchAlgorithm algorithm = PitchAlgorithm::Mpm;
  TrackerConfig tracker{};
};

struct PitchFrame {
  double timeSec = 0.0;  // thời điểm GIỮA cửa sổ, tính từ mẫu đầu vào đầu tiên
  PitchEstimate raw{};
  float rmsDb = -120.0f;
  float zcr = 0.0f;
  TrackedPitch tracked{};
};

// Chuỗi dò cao độ: hạ mẫu → cửa sổ trượt → MPM (hoặc YIN) → F0Tracker.
// Nhận audio ở tần số gốc theo từng block bất kỳ; mỗi khi đủ một hop thì phân tích một khung.
// Cấp phát trong constructor; process() không cấp phát.
class PitchAnalyzer {
 public:
  explicit PitchAnalyzer(const PitchAnalyzerConfig& cfg);

  const PitchAnalyzerConfig& config() const noexcept { return cfg_; }
  double analysisRate() const noexcept { return analysisRate_; }
  std::size_t window() const noexcept { return window_; }
  std::size_t hop() const noexcept { return hop_; }

  void reset() noexcept;

  // [RT] Đẩy `n` mẫu mono. Gọi onFrame(const PitchFrame&) cho mỗi khung phân tích xong.
  template <typename F>
  void process(const float* in, std::size_t n, F&& onFrame) noexcept {
    while (n > 0) {
      const std::size_t chunk = n < kChunk ? n : kChunk;
      const std::size_t produced = decimator_.process(in, chunk, dec_.data());
      for (std::size_t i = 0; i < produced; ++i) {
        if (pushSample(dec_[i])) onFrame(frame_);
      }
      in += chunk;
      n -= chunk;
    }
  }

 private:
  static constexpr std::size_t kChunk = 512;

  bool pushSample(float s) noexcept;  // true nếu vừa phân tích xong một khung (frame_)
  void analyzeFrame(const float* x) noexcept;

  PitchAnalyzerConfig cfg_;
  int factor_;
  double analysisRate_;
  std::size_t window_;
  std::size_t hop_;
  Decimator decimator_;
  std::unique_ptr<Mpm> mpm_;
  std::unique_ptr<Yin> yin_;
  F0Tracker tracker_;
  std::vector<float> dec_;
  std::vector<float> buf_;
  std::size_t fill_ = 0;
  std::size_t sinceHop_ = 0;
  uint64_t produced_ = 0;  // tổng số mẫu ở tần số phân tích
  PitchFrame frame_;
};

}  // namespace tonecore
