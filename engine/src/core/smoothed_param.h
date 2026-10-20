#pragma once

#include <atomic>
#include <cmath>

namespace tonecore {

// Tham số có làm mượt, dùng chung giữa luồng UI và audio thread.
//
// Luồng UI gọi setTarget() (atomic store, không khóa). Audio thread gọi next() mỗi mẫu:
// giá trị hiện tại tiến dần về đích theo bộ lọc thông thấp một cực (one-pole):
//
//     y[n] = y[n-1] + a * (đích - y[n-1]),   a = 1 - exp(-1 / (τ · fs))
//
// Sau khoảng 5τ giá trị đạt ~99% đích. Làm mượt tránh tiếng "lụp bụp" (zipper noise)
// khi người dùng kéo thanh trượt.
class SmoothedParam {
 public:
  SmoothedParam(float initial, float minValue, float maxValue)
      : min_(minValue), max_(maxValue), target_(clamp(initial)), current_(clamp(initial)) {}

  // Gọi khi biết sample rate (ngoài audio thread). τ tính bằng giây.
  void prepare(double sampleRate, double timeConstantSec) {
    coeff_ = timeConstantSec <= 0.0
                 ? 1.0f
                 : static_cast<float>(1.0 - std::exp(-1.0 / (timeConstantSec * sampleRate)));
    current_ = target_.load(std::memory_order_relaxed);
  }

  // Gọi từ luồng điều khiển.
  void setTarget(float v) noexcept { target_.store(clamp(v), std::memory_order_relaxed); }
  float target() const noexcept { return target_.load(std::memory_order_relaxed); }

  // [RT] Giá trị đã làm mượt cho mẫu tiếp theo.
  float next() noexcept {
    const float t = target_.load(std::memory_order_relaxed);
    const float stepped = current_ + coeff_ * (t - current_);
    // Chốt đúng đích khi đủ gần, hoặc khi bước nhảy nhỏ hơn độ phân giải float
    // (stepped == current_) – nếu không giá trị sẽ "kẹt" cách đích ~1e-5 mãi mãi.
    current_ = (stepped == current_ || std::fabs(t - stepped) < 1e-6f) ? t : stepped;
    return current_;
  }

  // [RT] true nếu đã ở đúng đích → có thể dùng đường xử lý nhanh không làm mượt.
  bool isSettled() const noexcept { return current_ == target_.load(std::memory_order_relaxed); }

  float current() const noexcept { return current_; }

 private:
  float clamp(float v) const noexcept {
    if (!(v >= min_)) return min_;  // cũng bắt NaN
    return v > max_ ? max_ : v;
  }

  const float min_;
  const float max_;
  std::atomic<float> target_;
  float current_;  // chỉ audio thread chạm vào sau prepare()
  float coeff_ = 1.0f;
};

}  // namespace tonecore
