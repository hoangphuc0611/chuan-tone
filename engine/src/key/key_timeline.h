#pragma once

#include <vector>

#include "key/chroma.h"
#include "key/key_detect.h"
#include "key/musical_key.h"

namespace tonecore {

// Theo dõi chuyển tone trong bài bằng HMM 24 trạng thái (12 chủ âm × trưởng/thứ) — ý tưởng của
// K. Noland, M. Sandler, "Key estimation using a hidden Markov model", ISMIR 2006 và
// H. Papadopoulos, G. Peeters, "Local key estimation from an audio signal relying on harmony and
// metrical structures", IEEE TASLP 2012.
//
//  1. Quan sát: cửa sổ `windowSec` trượt mỗi `hopSec`. Mỗi cửa sổ cộng dồn chroma (như dò tone
//     toàn bài) rồi tính điểm 24 tone s_t[k]. Log-likelihood phát xạ = s_t[k] / τ. Cửa sổ gần như
//     im lặng → phát xạ đều (không cho thông tin, HMM giữ tone cũ).
//  2. Chuyển trạng thái mỗi bước: ở lại tone cũ với xác suất 1 − ε; ε chia cho 23 tone còn lại,
//     tone "họ hàng" nặng gấp `relatedBoost` lần: quãng 5 lên/xuống, tương đối (Am ↔ C), song song
//     (Am ↔ A — bolero hay chuyển ở điệp khúc), nâng 1 hoặc 2 nửa cung cùng mode (nâng tone cuối
//     bài).
//  3. Viterbi → đường tone có xác suất cao nhất; forward–backward → xác suất hậu nghiệm từng bước,
//     độ tin cậy mỗi đoạn = trung bình hậu nghiệm của tone đoạn đó.
//  4. Gộp đoạn ngắn hơn `minSegmentSec` vào đoạn kề dài hơn (quy ước gán nhãn: chỉ tính chuyển tone
//     kéo dài ít nhất một câu nhạc). Còn đúng một đoạn → dùng tone toàn bài (ước lượng trên cả bài
//     ổn định hơn từng cửa sổ).
//
// Tham số mặc định (B5): ε = 0,002, τ = 0,1 — trên GuitarSet (không bài nào chuyển tone) giảm số
// bài bị đoán chuyển tone giả từ 43 xuống 19/360 so với ε = 0,01, τ = 0,05. Chọn lại trên tập dev
// nhạc Việt có chuyển tone ở B4.
struct KeyTimelineConfig {
  bool enabled = true;
  float windowSec = 6.0f;
  float hopSec = 2.0f;
  float emissionTemperature = 0.1f;  // τ
  float switchProb = 0.002f;         // ε, mỗi bước hopSec
  float relatedBoost = 10.0f;
  float minSegmentSec = 12.0f;
  float minWindowVoicedSec = 2.0f;  // cửa sổ có ít tiếng hơn → phát xạ đều
};

// true nếu b là tone "họ hàng" của a theo nghĩa ở trên (không tính a == b).
bool relatedKeys(const MusicalKey& a, const MusicalKey& b);

// Timeline phủ kín [0, durationSec). Rỗng nếu chroma rỗng hoặc profile không tồn tại.
// `globalKey`: tone toàn bài (estimateGlobalKey), dùng khi timeline chỉ còn một đoạn.
std::vector<KeySegment> estimateKeyTimeline(const Chromagram& ch, double durationSec,
                                            const KeyDetectConfig& keyCfg,
                                            const KeyTimelineConfig& cfg,
                                            const KeyEstimate& globalKey);

}  // namespace tonecore
