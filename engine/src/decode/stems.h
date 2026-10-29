#pragma once

#include <string>
#include <vector>

#include "decode/decoder.h"

namespace tonecore {

// Bài gốc + stem giọng do server tách (M6, ADR-006) → hai phần cùng tần số, cùng số kênh, cùng độ
// dài với bài gốc như codec của máy giải mã ra:
//   - giọng: ghép các đoạn theo thứ tự (FLAC tc_exporter gửi lên, server trả cùng ranh giới), đổi
//     về tần số bài gốc nếu khác (bài 48 kHz gửi đi ở 44,1 kHz), cắt / đệm vài mẫu lệch do đổi tần
//     số hai lần;
//   - phần nhạc = bài − giọng (đúng cách "no_vocals" của Demucs), làm tròn + kẹp 16 bit.
// Stem giọng tính từ đúng PCM máy đã giải mã → bài − giọng triệt tiêu chính xác (cùng tần số).
// Lỗi: như decodeFile; TC_ERR_INVALID_ARG khi độ dài / số kênh của giọng không khớp bài.
// Luồng nền.
tc_result decodeStems(const std::string& mixPath, const std::vector<std::string>& vocalPaths,
                      DecodedAudio& instrumental, DecodedAudio& vocal);

// Lệch độ dài tối đa (mẫu, ở tần số bài gốc) chấp nhận giữa giọng và bài sau khi đổi tần số.
inline constexpr uint64_t kMaxStemLengthSlack = 1024;

}  // namespace tonecore
