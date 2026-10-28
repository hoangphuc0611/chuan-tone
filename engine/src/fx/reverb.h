#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace tonecore {

enum class ReverbType : int32_t { Room = 0, Hall = 1, Plate = 2 };

struct ReverbParams {
  ReverbType type = ReverbType::Hall;
  float decaySec = 2.2f;     // RT60 ở tần số thấp / giữa
  float damping = 0.5f;      // 0…0,9: RT60 ở 4 kHz = decaySec · (1 − damping) (tiếng vang tối dần)
  float preDelayMs = 30.0f;  // 0…100
  float mix = 0.22f;         // độ lớn phần vang so với tiếng khô
};

// Reverb giọng (CLAUDE.md 3.7): mạng trễ hồi tiếp (FDN) 8 dòng — J.-M. Jot, A. Chaigne, "Digital
// delay networks for designing artificial reverberators", AES 1991; khuếch tán all-pass của
// Schroeder (1962). Mono vào → stereo ra, chỉ xuất phần VANG (phần khô do chuỗi FX giữ).
//  1. Pre-delay (dòng trễ nguyên mẫu).
//  2. 4 all-pass nối tiếp (g = 0,5): mỗi tiếng vào thành nhiều tiếng dội dày
//  ngay
//     từ đầu (không "lộp độp").
//  3. FDN: 8 dòng trễ m_i (số nguyên tố, rải theo cấp số nhân trong khoảng của kiểu phòng — không
//     chung ước → các mode không trùng nhau, không "kim loại"); ma trận hồi tiếp Hadamard 8×8 / √8
//     (trực giao: không mất / không thêm năng lượng, tính bằng biến đổi Walsh–Hadamard nhanh).
//  4. Hấp thụ mỗi dòng: H_i(z) = c_i·(1 − b_i)/(1 − b_i·z⁻¹) với c_i = 10^(−3·m_i/(fs·T_thấp)), b_i
//     giải sao cho |H_i| tại 4 kHz = 10^(−3·m_i/(fs·T_cao)) → RT60 đúng T_thấp ở tần số thấp, T_cao
//     ở 4 kHz (cao hơn còn ngắn hơn, như hấp thụ của không khí).
//     Tiếng vào nhân 1/√(trung bình c_i²/(1 − c_i²)) → năng lượng đáp ứng xung mỗi kênh ≈ 1.
//  5. Ra: L, R = hai tổ hợp dấu trực giao của 8 dòng (/√8) → hai kênh gần như không tương quan.
// mix, decay, damping trượt dần (τ 20 ms cho mix; hệ số hấp thụ tính lại khi đổi); đổi kiểu phòng /
// pre-delay thì áp ngay (khi đang phát, đi đường đổi preset). Cấp phát: chỉ trong constructor.
class Reverb {
 public:
  static constexpr int kLines = 8;
  static constexpr int kDiffusers = 4;
  static constexpr double kMaxPreDelaySec = 0.1;

  explicit Reverb(double sampleRate);

  void setParams(const ReverbParams& p) noexcept;  // [RT]
  void reset() noexcept;
  // [RT] in: mono; outL / outR: phần vang (ghi đè).
  void process(const float* in, float* outL, float* outR, uint32_t n) noexcept;

  // Độ dài các dòng FDN đang dùng (mẫu) — cho test / ghi chép.
  const std::array<uint32_t, kLines>& lineLengths() const noexcept { return len_; }

 private:
  struct Line {
    std::vector<float> buf;
    uint32_t mask = 0;
  };
  struct Diffuser {
    std::vector<float> buf;
    uint32_t mask = 0;
    uint32_t len = 1;
  };

  void configure(ReverbType type) noexcept;
  void updateAbsorption() noexcept;

  double fs_;
  ReverbParams p_;
  bool configured_ = false;
  std::array<Line, kLines> lines_;
  std::array<uint32_t, kLines> len_{};
  std::array<float, kLines> c_{};      // độ lợi DC mỗi vòng
  std::array<float, kLines> b_{};      // cực của bộ lọc hấp thụ
  std::array<float, kLines> state_{};  // trạng thái bộ lọc hấp thụ
  std::array<Diffuser, kDiffusers> diff_;
  float diffGain_ = 0.7f;
  std::vector<float> pre_;
  uint32_t preMask_ = 0;
  uint32_t preLen_ = 0;
  uint32_t pos_ = 0;     // chỉ số ghi chung (mọi vòng đệm cùng tiến)
  float inGain_ = 1.0f;  // chuẩn hoá năng lượng (updateAbsorption)
  float mix_ = 0.0f;
  float mixTarget_ = 0.0f;
  float smooth_ = 1.0f;
};

}  // namespace tonecore
