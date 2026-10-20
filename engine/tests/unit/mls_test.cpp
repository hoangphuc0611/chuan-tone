#include "latency/mls.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

using tonecore::generateMls;

TEST(Mls, UnsupportedOrderReturnsEmpty) {
  EXPECT_TRUE(generateMls(3).empty());
  EXPECT_TRUE(generateMls(20).empty());
}

// Với mọi bậc hỗ trợ: độ dài 2^n − 1, số +1 nhiều hơn số −1 đúng một đơn vị, và tự tương quan
// vòng bằng N tại 0, bằng −1 ở mọi độ trễ khác. Đây là kiểm chứng đa thức nguyên thủy đúng.
class MlsOrderTest : public ::testing::TestWithParam<int> {};

TEST_P(MlsOrderTest, HasIdealCircularAutocorrelation) {
  const int order = GetParam();
  const auto mls = generateMls(order);
  const auto n = static_cast<int64_t>((1 << order) - 1);
  ASSERT_EQ(static_cast<int64_t>(mls.size()), n);

  int64_t sum = 0;
  for (float v : mls) {
    ASSERT_TRUE(v == 1.0f || v == -1.0f);
    sum += v > 0 ? 1 : -1;
  }
  EXPECT_EQ(sum, 1);

  // Kiểm tra một số độ trễ đại diện (kiểm toàn bộ quá chậm với bậc 16 ở bản Debug).
  const std::set<int64_t> lags = {0, 1, 2, 3, n / 3, n / 2, n - 1};
  for (int64_t lag : lags) {
    int64_t acc = 0;
    for (int64_t i = 0; i < n; ++i) {
      acc += static_cast<int64_t>(mls[static_cast<size_t>(i)] *
                                  mls[static_cast<size_t>((i + lag) % n)]);
    }
    EXPECT_EQ(acc, lag == 0 ? n : -1) << "order=" << order << " lag=" << lag;
  }
}

INSTANTIATE_TEST_SUITE_P(AllOrders, MlsOrderTest, ::testing::Range(8, 17));
