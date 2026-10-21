#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tonecore {

struct PsolaConfig {
  double sampleRate = 48000.0;
  int latency = 480;    // độ trễ cố định (mẫu) — 10 ms ở 48 kHz (ADR-002)
  double minF0 = 70.0;  // chu kỳ dài nhất cần xử lý
  int crossfade = 240;  // chuyển hữu thanh ↔ vô thanh (5 ms)
};

// Dịch cao độ giọng hát TD-PSOLA (Moulines & Charpentier 1990), chạy real-time với độ trễ CỐ
// ĐỊNH, tự giữ formant (không bị giọng "chipmunk").
//
// Ý tưởng: cắt tín hiệu thành các "hạt" (grain) dài 2 chu kỳ, tâm đặt tại các mốc chu kỳ (pitch
// mark) trên tín hiệu vào, nhân cửa sổ Hann, rồi đặt lại các hạt cách nhau T/ratio trên trục ra.
// Hạt dày hơn → cao độ cao hơn; hình dạng mỗi hạt (đường bao phổ = formant) giữ nguyên.
//
// Chi tiết real-time:
//  - Trục thời gian: mẫu ra thứ j ứng với vị trí u = j − latency trên trục vào.
//  - Mốc phân tích: mốc kế = mốc trước + T, tinh chỉnh trong ±T/4 để khớp dạng sóng với chu kỳ
//    trước (tương quan chuẩn hoá) → các mốc bám đúng cùng một pha của dạng sóng. Mốc chỉ được đặt
//    khi đã có đủ tín hiệu tới mốc + T/4.
//  - Mốc tổng hợp: s_k+1 = s_k + T/ratio. Khi trục ra chạm s − H (đầu hạt), chọn mốc phân tích
//    gần s nhất trong các mốc đã biết. Hạt được đọc dần từng mẫu nên chỉ cần mốc đó cách s không
//    quá `latency` về phía trước.
//  - Nửa độ dài hạt H = T, nhưng với giọng trầm (T > latency/1,25, tức f0 < ~125 Hz) thì cắt
//    ngắn còn max(0,6·T, latency − T/4) để vẫn giữ độ trễ cố định.
//  - Chồng hạt có CHUẨN HOÁ theo tổng cửa sổ (out = Σ w·x / Σ w) → biên độ ổn định với mọi tỉ lệ
//    và với hạt bị cắt ngắn.
//  - Đoạn vô thanh (phụ âm, hơi thở) đi thẳng (trễ đúng `latency`), chuyển qua lại có crossfade.
// Cấp phát trong constructor; process() không cấp phát, không khoá.
class Psola {
 public:
  explicit Psola(const PsolaConfig& cfg);

  int latency() const noexcept { return cfg_.latency; }
  void reset() noexcept;

  // [RT] Cập nhật kết quả dò cao độ (gọi mỗi hop). periodSamples tính theo mẫu ở sampleRate.
  void setAnalysis(bool voiced, float periodSamples) noexcept;
  // [RT] Tỉ lệ cao độ ra/vào (kẹp 0,5–2).
  void setRatio(float ratio) noexcept;

  // [RT] in → out, n mẫu (in và out có thể trùng nhau).
  void process(const float* in, float* out, std::size_t n) noexcept;

  // Thống kê cho test: số mốc phân tích đã đặt.
  uint64_t marksPlaced() const noexcept { return marksPlaced_; }

 private:
  struct Mark {
    int64_t pos = 0;
    float period = 0.0f;
  };
  struct Grain {
    int64_t a = 0;  // tâm trên trục vào (mốc phân tích)
    int64_t s = 0;  // tâm trên trục ra (theo toạ độ u)
    int h = 0;      // nửa độ dài
  };

  float at(int64_t i) const noexcept { return ring_[static_cast<std::size_t>(i) & mask_]; }
  float hann(float t) const noexcept;  // t ∈ [−1, 1]
  void placeMarks() noexcept;
  int64_t refineMark(int64_t pred, float period) const noexcept;
  void startGrains(int64_t u) noexcept;

  PsolaConfig cfg_;
  int maxPeriod_;
  std::vector<float> ring_;
  std::size_t mask_;
  std::vector<float> hannTable_;

  int64_t n_ = 0;  // số mẫu vào đã ghi

  // Trạng thái dò cao độ (từ setAnalysis).
  bool voiced_ = false;
  float period_ = 0.0f;
  float ratio_ = 1.0f;

  // Mốc phân tích.
  static constexpr int kMaxMarks = 64;
  Mark marks_[kMaxMarks];
  int markHead_ = 0;  // vị trí ghi kế tiếp
  int markCount_ = 0;
  bool chain_ = false;  // đang có chuỗi mốc liên tục
  int64_t lastMark_ = 0;
  uint64_t marksPlaced_ = 0;

  // Hạt đang phát.
  static constexpr int kMaxGrains = 8;
  Grain grains_[kMaxGrains];
  int grainCount_ = 0;
  double nextSynth_ = 0.0;  // tâm hạt tổng hợp kế tiếp (toạ độ u)
  bool synthActive_ = false;

  float wetMix_ = 0.0f;
};

}  // namespace tonecore
