#pragma once

#include <vector>

namespace tonecore {

// Sinh chuỗi MLS (Maximum Length Sequence) bậc `order` (8..16), độ dài 2^order − 1, giá trị ±1.
//
// MLS sinh bởi thanh ghi dịch hồi tiếp tuyến tính (LFSR) với đa thức nguyên thủy, có tính chất:
// tự tương quan vòng bằng N tại độ trễ 0 và bằng −1 ở mọi độ trễ khác → gần như một xung
// lý tưởng, nhưng năng lượng trải đều theo thời gian nên phát qua loa an toàn hơn phát xung
// và chống nhiễu tốt hơn nhiều.
// Tham khảo: Rife & Vanderkooy, "Transfer-Function Measurement with Maximum-Length
// Sequences", JAES 1989.
//
// Trả về vector rỗng nếu `order` không được hỗ trợ.
std::vector<float> generateMls(int order);

}  // namespace tonecore
