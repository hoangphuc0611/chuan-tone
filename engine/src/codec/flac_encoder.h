#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tonecore/tonecore.h"

namespace tonecore {

// Bộ nén FLAC 16-bit tự viết theo đặc tả công khai RFC 9639 (thay cho libFLAC: không thêm thư viện
// C vào bốn nền tảng build — docs/m6-plan.md B2). Dùng để gửi bài lên server tách giọng: không mất
// dữ liệu; beat karaoke thật còn ~62% kích thước PCM (bộ nén FLAC của Apple: 62–63%).
//
// Mỗi khung (frame) `blockSize` mẫu mỗi kênh:
//  1. Stereo: thử bốn cách ghép kênh (trái / phải, trái / hiệu, hiệu / phải, giữa / hiệu với
//     giữa = (L + R) >> 1, hiệu = L − R — kênh hiệu cần 17 bit), chọn cách ước tốn ít bit nhất.
//  2. Mỗi kênh con (subframe): toàn bộ bằng nhau → CONSTANT; còn lại thử bộ dự đoán cố định bậc 0–4
//     (sai phân bậc k: e = Δᵏx, chọn bậc có Σ|e| nhỏ nhất) và dự đoán tuyến tính LPC bậc ≤ 8 (cửa
//     sổ Tukey, Levinson–Durbin, hệ số 12 bit), lấy cách ít bit hơn; không lợi hơn ghi thẳng →
//     VERBATIM.
//  3. Phần dư mã Rice chia vùng: chia khối thành 2^p vùng, mỗi vùng một tham số k; số dư gấp dấu
//     u = 2e (e ≥ 0) hoặc −2e − 1 (e < 0) ghi thương u >> k dạng unary rồi k bit thấp. p và k chọn
//     theo ước lượng số bit n·(k + 1) + Σu / 2^k; k > 14 thì dùng mã RICE2 (tham số 5 bit).
//  4. Khung kết thúc bằng CRC-16 (đa thức 0x8005), đầu khung có CRC-8 (0x07).
// Không tính MD5 (STREAMINFO ghi 0 = "không rõ", được phép theo RFC 9639 mục 8.2).
struct FlacEncodeOptions {
  uint32_t blockSize = 4096;
  int maxPartitionOrder = 8;
};

// PCM 16-bit interleaved (1–8 kênh, `frames` mẫu mỗi kênh) → nguyên file FLAC trong bộ nhớ.
// Ném std::invalid_argument nếu tham số sai (0 kênh, > 8 kênh, tần số 0 hoặc > 655350 Hz).
std::vector<uint8_t> encodeFlac(const int16_t* interleaved, uint64_t frames, uint32_t channels,
                                uint32_t sampleRate, const FlacEncodeOptions& options = {});

// Ghi file qua tên tạm rồi đổi tên: người đọc (luồng upload) không bao giờ thấy file dở.
tc_result writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data);

}  // namespace tonecore
