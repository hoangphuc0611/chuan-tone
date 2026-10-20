#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace tonecore {

// Kích thước cache line dùng để tách chỉ số đọc/ghi, tránh false sharing.
inline constexpr std::size_t kCacheLine = 64;

// Ring buffer lock-free một luồng ghi – một luồng đọc (SPSC).
//
// - Bộ nhớ cấp phát một lần trong constructor; push/pop không cấp phát, không khóa → dùng
//   được trong audio callback.
// - Dung lượng làm tròn lên lũy thừa của 2 để dùng phép AND thay cho chia lấy dư.
// - Chỉ số head_/tail_ tăng đơn điệu (không quấn lại); hiệu head - tail là số phần tử.
//   Với uint64_t thì không bao giờ tràn trong thực tế.
// - Thứ tự bộ nhớ: bên ghi publish dữ liệu bằng store-release trên head_, bên đọc
//   acquire head_ trước khi đọc dữ liệu (và ngược lại với tail_).
template <typename T>
class SpscRing {
  static_assert(std::is_trivially_copyable_v<T>, "SpscRing chỉ chứa kiểu sao chép tầm thường");

 public:
  explicit SpscRing(std::size_t minCapacity)
      : capacity_(roundUpPow2(minCapacity < 2 ? 2 : minCapacity)),
        mask_(capacity_ - 1),
        data_(new T[capacity_]()) {}

  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  std::size_t capacity() const noexcept { return capacity_; }

  // Gọi từ luồng ghi. Ghi tối đa `count` phần tử, trả về số đã ghi. [RT]
  std::size_t push(const T* src, std::size_t count) noexcept {
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t freeSpace = capacity_ - static_cast<std::size_t>(head - tail);
    const std::size_t n = count < freeSpace ? count : freeSpace;
    for (std::size_t i = 0; i < n; ++i) data_[(head + i) & mask_] = src[i];
    head_.store(head + n, std::memory_order_release);
    return n;
  }

  // Gọi từ luồng đọc. Đọc tối đa `count` phần tử, trả về số đã đọc. [RT]
  std::size_t pop(T* dst, std::size_t count) noexcept {
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    const std::size_t avail = static_cast<std::size_t>(head - tail);
    const std::size_t n = count < avail ? count : avail;
    for (std::size_t i = 0; i < n; ++i) dst[i] = data_[(tail + i) & mask_];
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  bool push(const T& v) noexcept { return push(&v, 1) == 1; }
  bool pop(T& v) noexcept { return pop(&v, 1) == 1; }

  // Ước lượng; chính xác khi gọi từ một trong hai luồng chủ sở hữu.
  std::size_t sizeApprox() const noexcept {
    return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                    tail_.load(std::memory_order_acquire));
  }

 private:
  static std::size_t roundUpPow2(std::size_t v) {
    std::size_t p = 1;
    while (p < v) p <<= 1;
    return p;
  }

  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<T[]> data_;
  alignas(kCacheLine) std::atomic<uint64_t> head_{0};  // luồng ghi sở hữu
  alignas(kCacheLine) std::atomic<uint64_t> tail_{0};  // luồng đọc sở hữu
};

}  // namespace tonecore
