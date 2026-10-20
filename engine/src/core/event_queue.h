#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

#include "tonecore/tonecore.h"

namespace tonecore {

// Hàng đợi sự kiện gửi lên UI (tc_poll_events).
//
// Chỉ luồng nền / luồng hệ điều hành đẩy sự kiện vào (nạp beat xong, đổi route, đo độ trễ
// xong...) — KHÔNG BAO GIỜ audio thread. Vì vậy dùng mutex là an toàn và đơn giản.
// Audio thread chỉ bật cờ atomic; luồng điều khiển đọc cờ và đẩy sự kiện tương ứng.
class EventQueue {
 public:
  explicit EventQueue(std::size_t capacity = 256) : capacity_(capacity) {}

  void push(const tc_event& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() >= capacity_) events_.pop_front();  // bỏ sự kiện cũ nhất khi đầy
    events_.push_back(e);
  }

  void push(int32_t type, int32_t code = 0, double value = 0.0) {
    tc_event e{};
    e.type = type;
    e.code = code;
    e.value = value;
    push(e);
  }

  std::size_t pop(tc_event* out, std::size_t max) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    while (n < max && !events_.empty()) {
      out[n++] = events_.front();
      events_.pop_front();
    }
    return n;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<tc_event> events_;
};

}  // namespace tonecore
