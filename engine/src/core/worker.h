#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace tonecore {

// Một luồng nền chạy lần lượt các việc nặng: giải mã beat, phân tích độ trễ, huỷ buffer cũ...
// Không bao giờ dùng từ audio thread.
class Worker {
 public:
  Worker() : thread_([this] { run(); }) {}

  ~Worker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    cv_.notify_all();
    thread_.join();  // việc đang chạy được làm nốt; việc còn trong hàng đợi bị bỏ
  }

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void post(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

  // Chờ đến khi hàng đợi rỗng và không còn việc đang chạy (dùng trong test).
  void waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this] { return jobs_.empty() && !busy_; });
  }

 private:
  void run() {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_) return;
        job = std::move(jobs_.front());
        jobs_.pop_front();
        busy_ = true;
      }
      job();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
      }
      idleCv_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable idleCv_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
  bool busy_ = false;
  std::thread thread_;  // khai báo cuối: khởi tạo sau các thành viên khác
};

}  // namespace tonecore
