#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "dsp/biquad.h"
#include "pitch/corrector.h"
#include "pitch/pitch_analyzer.h"
#include "pitch/pitch_state.h"
#include "pitch/presets.h"
#include "pitch/psola.h"
#include "pitch/scales.h"

namespace tonecore {

// Khối "Tự chỉnh giọng" trên đường micro:
//
//   mic → HPF 80 Hz ─┬────────────────────────────────────────────── khô (không trễ) ─┐
//                    ├→ hạ mẫu → MPM → F0Tracker → PitchCorrector ─┐                  ├→ trộn
//                    └──────────────────────────────→ TD-PSOLA (trễ 10 ms) ─ ướt ──────┘
//
// - Dò cao độ luôn chạy (để đồng hồ cao độ hoạt động cả khi tắt chỉnh giọng).
// - Bật/tắt chỉnh giọng = chuyển dần giữa đường khô (không trễ) và đường ướt (trễ 10 ms) trong
//   20 ms (ADR-002: khi tắt thì bỏ hẳn độ trễ).
// - Tham số đổi từ luồng điều khiển qua atomic; audio thread đọc mỗi block.
// - Xử lý theo từng đoạn ≤ 1 hop (4 ms) để kết quả dò cao độ được áp kịp thời cho PSOLA.
class VoicePitchNode {
 public:
  VoicePitchNode(double sampleRate, uint32_t maxBlockFrames);

  // ---- Luồng điều khiển ----
  void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
  bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

  void setKey(int root, Scale scale) noexcept;
  int keyRoot() const noexcept { return keyRoot_.load(std::memory_order_relaxed); }
  Scale keyScale() const noexcept { return static_cast<Scale>(keyScale_.load()); }

  void applyPreset(CorrectionPreset p) noexcept;
  void setRetuneMs(float v) noexcept;
  void setHumanize(float v) noexcept;
  void setFlexCents(float v) noexcept;
  void setVibratoPreserve(float v) noexcept;
  void setHysteresisCents(float v) noexcept;
  CorrectorParams params() const noexcept;

  bool readState(PitchSnapshot& out) const noexcept { return state_.read(out); }

  // Độ trễ thuật toán hiện tại (mẫu): 0 khi tắt, latency của PSOLA khi bật.
  int algorithmLatency() const noexcept { return enabled() ? psola_.latency() : 0; }
  int psolaLatency() const noexcept { return psola_.latency(); }

  // ---- Audio thread ----
  // [RT] Xử lý tại chỗ `n` mẫu mono (n ≤ maxBlockFrames).
  void process(float* io, uint32_t n) noexcept;

 private:
  void syncParams() noexcept;
  void onFrame(const PitchFrame& f) noexcept;
  void processSlice(float* io, uint32_t n) noexcept;

  double sampleRate_;
  Biquad hpf_;
  PitchAnalyzer analyzer_;
  PitchCorrector corrector_;
  Psola psola_;
  std::vector<float> wet_;
  uint32_t slice_;

  // Tham số (luồng điều khiển ghi, audio thread đọc).
  std::atomic<bool> enabled_{false};
  std::atomic<int> keyRoot_{0};
  std::atomic<int> keyScale_{static_cast<int>(Scale::Chromatic)};
  std::atomic<float> retuneMs_;
  std::atomic<float> humanize_;
  std::atomic<float> flexCents_;
  std::atomic<float> vibratoPreserve_;
  std::atomic<float> hysteresisCents_;
  std::atomic<uint32_t> paramVersion_{1};
  uint32_t seenVersion_ = 0;  // audio thread

  float mix_ = 0.0f;  // 0 = khô, 1 = ướt (audio thread)
  float mixStep_;
  PitchSnapshot snap_;  // audio thread
  PitchStateChannel state_;
};

}  // namespace tonecore
