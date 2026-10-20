#include "core/spsc_ring.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

using tonecore::SpscRing;

TEST(SpscRing, CapacityRoundsUpToPowerOfTwo) {
  EXPECT_EQ(SpscRing<int>(5).capacity(), 8u);
  EXPECT_EQ(SpscRing<int>(8).capacity(), 8u);
  EXPECT_EQ(SpscRing<int>(0).capacity(), 2u);
}

TEST(SpscRing, PushPopSingleItems) {
  SpscRing<int> ring(4);
  int v = 0;
  EXPECT_FALSE(ring.pop(v));
  EXPECT_TRUE(ring.push(1));
  EXPECT_TRUE(ring.push(2));
  EXPECT_EQ(ring.sizeApprox(), 2u);
  ASSERT_TRUE(ring.pop(v));
  EXPECT_EQ(v, 1);
  ASSERT_TRUE(ring.pop(v));
  EXPECT_EQ(v, 2);
  EXPECT_FALSE(ring.pop(v));
}

TEST(SpscRing, RejectsWhenFull) {
  SpscRing<int> ring(4);
  const int data[6] = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(ring.push(data, 6), 4u);  // chỉ ghi được bằng dung lượng
  EXPECT_FALSE(ring.push(7));
  int out[6] = {};
  EXPECT_EQ(ring.pop(out, 6), 4u);
  EXPECT_EQ(out[0], 1);
  EXPECT_EQ(out[3], 4);
}

TEST(SpscRing, WrapsAroundCorrectly) {
  SpscRing<float> ring(8);
  float in[5];
  float out[5];
  float next = 0.0f;
  float expect = 0.0f;
  // Ghi/đọc nhiều vòng để chỉ số quấn qua biên của mảng.
  for (int round = 0; round < 100; ++round) {
    for (float& x : in) x = next++;
    ASSERT_EQ(ring.push(in, 5), 5u);
    ASSERT_EQ(ring.pop(out, 5), 5u);
    for (float x : out) EXPECT_EQ(x, expect++);
  }
}

// Một luồng ghi, một luồng đọc, kiểm tra thứ tự và không mất dữ liệu.
// Chạy với ThreadSanitizer trong CI (preset host-tsan).
TEST(SpscRing, ConcurrentProducerConsumerPreservesOrder) {
  constexpr uint32_t kTotal = 1u << 20;
  SpscRing<uint32_t> ring(1024);

  std::thread producer([&] {
    uint32_t buf[64];
    uint32_t next = 0;
    while (next < kTotal) {
      uint32_t n = 0;
      while (n < 64 && next + n < kTotal) {
        buf[n] = next + n;
        ++n;
      }
      const auto written = static_cast<uint32_t>(ring.push(buf, n));
      next += written;
      if (written == 0) std::this_thread::yield();
    }
  });

  uint32_t expected = 0;
  bool ordered = true;
  uint32_t buf[97];  // cỡ lẻ để block đọc/ghi lệch nhau
  while (expected < kTotal) {
    const auto n = ring.pop(buf, 97);
    for (std::size_t i = 0; i < n; ++i) {
      if (buf[i] != expected) ordered = false;
      ++expected;
    }
    if (n == 0) std::this_thread::yield();
  }
  producer.join();

  EXPECT_TRUE(ordered);
  EXPECT_EQ(expected, kTotal);
  EXPECT_EQ(ring.sizeApprox(), 0u);
}
