#pragma once

#include <atomic>
#include <cstdint>

namespace tonecore {

// Ảnh chụp trạng thái cao độ cho UI (đồng hồ cao độ).
struct PitchSnapshot {
  bool voiced = false;
  float f0 = 0.0f;               // Hz
  float confidence = 0.0f;       // 0..1
  float inputMidi = 0.0f;        // cao độ người hát (MIDI thực)
  int32_t targetNote = -1;       // nốt đích (MIDI), -1 nếu chưa có
  float centsOff = 0.0f;         // người hát lệch so với nốt đích
  float correctionCents = 0.0f;  // engine đang dịch bao nhiêu cent
};

// Kênh một-ghi-nhiều-đọc không khoá kiểu seqlock (Lameter, "Effective synchronization on Linux/
// NUMA systems", 2005): audio thread ghi, luồng UI đọc. Bộ đếm lẻ = đang ghi; người đọc thử lại
// nếu bộ đếm đổi giữa chừng. Mọi trường là atomic → không có data race theo mô hình bộ nhớ C++.
class PitchStateChannel {
 public:
  // [RT] Chỉ một luồng ghi.
  void publish(const PitchSnapshot& s) noexcept {
    const uint32_t seq = seq_.load(std::memory_order_relaxed);
    seq_.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    voiced_.store(s.voiced, std::memory_order_relaxed);
    f0_.store(s.f0, std::memory_order_relaxed);
    confidence_.store(s.confidence, std::memory_order_relaxed);
    inputMidi_.store(s.inputMidi, std::memory_order_relaxed);
    targetNote_.store(s.targetNote, std::memory_order_relaxed);
    centsOff_.store(s.centsOff, std::memory_order_relaxed);
    correctionCents_.store(s.correctionCents, std::memory_order_relaxed);
    seq_.store(seq + 2, std::memory_order_release);
  }

  // Trả về false nếu không đọc được bản nhất quán sau vài lần thử (hiếm).
  bool read(PitchSnapshot& out) const noexcept {
    for (int attempt = 0; attempt < 8; ++attempt) {
      const uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 & 1u) continue;
      PitchSnapshot tmp;
      tmp.voiced = voiced_.load(std::memory_order_relaxed);
      tmp.f0 = f0_.load(std::memory_order_relaxed);
      tmp.confidence = confidence_.load(std::memory_order_relaxed);
      tmp.inputMidi = inputMidi_.load(std::memory_order_relaxed);
      tmp.targetNote = targetNote_.load(std::memory_order_relaxed);
      tmp.centsOff = centsOff_.load(std::memory_order_relaxed);
      tmp.correctionCents = correctionCents_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == s1) {
        out = tmp;
        return true;
      }
    }
    return false;
  }

 private:
  std::atomic<uint32_t> seq_{0};
  std::atomic<bool> voiced_{false};
  std::atomic<float> f0_{0.0f};
  std::atomic<float> confidence_{0.0f};
  std::atomic<float> inputMidi_{0.0f};
  std::atomic<int32_t> targetNote_{-1};
  std::atomic<float> centsOff_{0.0f};
  std::atomic<float> correctionCents_{0.0f};
};

}  // namespace tonecore
