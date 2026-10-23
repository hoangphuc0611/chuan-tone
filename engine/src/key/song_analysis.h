#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "key/analysis_common.h"
#include "key/chroma.h"
#include "key/cqt.h"
#include "key/hpss.h"
#include "tonecore/tonecore.h"

namespace tonecore {

enum class KeyMode : int32_t { Major = 0, Minor = 1 };

struct MusicalKey {
  int32_t root = 0;  // 0 = Đô (C) … 11 = Si (B)
  KeyMode mode = KeyMode::Major;
};

// Một đoạn của timeline tone: [startSec, endSec).
struct KeySegment {
  double startSec = 0.0;
  double endSec = 0.0;
  MusicalKey key{};
  float confidence = 0.0f;  // 0..1
};

// Các giai đoạn phân tích, theo thứ tự chạy. Sẽ thêm dần ở B3–B6 (tone, BPM, loudness).
enum class AnalysisStage : int32_t {
  Idle = 0,
  Decoding = 1,    // giải mã file bằng codec của hệ điều hành
  Resampling = 2,  // trộn mono + đổi về tần số phân tích
  Separating = 3,  // HPSS (tách hoà âm / bộ gõ) + ước lượng tuning
  Spectrum = 4,    // CQT phần hoà âm → chroma
  Done = 5
};

struct StageTiming {
  AnalysisStage stage = AnalysisStage::Idle;
  double ms = 0.0;
};

// Kết quả phân tích một bài. Trường optional rỗng = chưa dò (bước chưa làm) hoặc không dò được
// (vd. bài chỉ có trống thì không có tone).
struct SongAnalysis {
  double durationSec = 0.0;
  uint32_t sourceRate = 0;
  uint32_t channels = 0;
  double analysisRate = 0.0;

  std::optional<MusicalKey> key;  // tone chính của bài
  float keyConfidence = 0.0f;
  std::vector<KeySegment> timeline;  // theo dõi chuyển tone; rỗng nếu chưa dò

  std::optional<float> tuningCents;  // độ lệch so với La = 440 Hz, −50…+50
  Chromagram chroma;                 // chroma bass/treble của phần hoà âm (đầu vào dò tone)
  std::optional<float> bpm;
  float bpmConfidence = 0.0f;
  std::optional<float> integratedLufs;  // ITU-R BS.1770-4
  std::optional<float> samplePeakDb;

  std::vector<StageTiming> timings;
};

struct AnalysisConfig {
  // Tần số phân tích mong muốn: đủ dải cho chroma (≤ ~4 kHz) và onset, rẻ hơn ~4 lần so với
  // 48 kHz. Tần số thực = tần số gốc / round(gốc / mong muốn) (48 kHz → 12 kHz,
  // 44,1 kHz → 11025 Hz) để hạ mẫu bằng hệ số nguyên — rẻ hơn ~10 lần so với resample tỉ lệ lẻ.
  double targetAnalysisRate = 11025.0;
  HpssConfig hpss;
  CqtConfig cqt;  // tuningCents bị ghi đè bằng tuning ước lượng được
  ChromaConfig chroma;
};

// Kênh trao đổi giữa luồng phân tích và luồng điều khiển (UI / C API): tiến độ, giai đoạn, huỷ.
// Mọi hàm đều lock-free, gọi được từ bất kỳ luồng nào.
class AnalysisControl {
 public:
  void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }
  bool cancelled() const noexcept { return cancel_.load(std::memory_order_relaxed); }

  // Tiến độ tổng 0..1, chỉ tăng.
  float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
  AnalysisStage stage() const noexcept {
    return static_cast<AnalysisStage>(stage_.load(std::memory_order_relaxed));
  }

  // Luồng phân tích gọi. Tiến độ nhỏ hơn giá trị cũ bị bỏ qua (giữ đơn điệu).
  void report(AnalysisStage stage, float progress) noexcept {
    stage_.store(static_cast<int32_t>(stage), std::memory_order_relaxed);
    float old = progress_.load(std::memory_order_relaxed);
    while (progress > old &&
           !progress_.compare_exchange_weak(old, progress, std::memory_order_relaxed)) {
    }
  }

 private:
  std::atomic<bool> cancel_{false};
  std::atomic<float> progress_{0.0f};
  std::atomic<int32_t> stage_{static_cast<int32_t>(AnalysisStage::Idle)};
};

}  // namespace tonecore
