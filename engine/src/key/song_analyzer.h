#pragma once

#include <functional>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "key/song_analysis.h"

namespace tonecore {

// Phân tích offline cả bài: giải mã → mono ~11–12 kHz → HPSS + tuning → CQT → chroma → tone +
// timeline chuyển tone; LUFS trên tín hiệu gốc; BPM từ phần bộ gõ. Chạy đồng bộ trên luồng gọi —
// luôn là luồng nền, KHÔNG BAO GIỜ từ audio thread (có cấp phát, có I/O). Mỗi lần gọi dùng đối
// tượng riêng nên nhiều luồng chạy song song được.
//
// Trả về TC_OK, kResultCancelled khi ctl.cancel() được gọi, hoặc mã lỗi của bộ giải mã.
tc_result analyzeFile(const std::string& path, const AnalysisConfig& cfg, AnalysisControl& ctl,
                      SongAnalysis& out);

// Như trên với audio đã giải mã (test dùng tín hiệu tổng hợp, không cần ghi file).
tc_result analyzeDecoded(DecodedAudio&& audio, const AnalysisConfig& cfg, AnalysisControl& ctl,
                         SongAnalysis& out);

// Tín hiệu mono đưa vào các bước phân tích, biên độ ±1.
struct AnalysisSignal {
  std::vector<float> samples;
  double rate = 0.0;
};

// Hệ số hạ mẫu nguyên cho tần số gốc `sourceRate` (≥ 1).
int analysisDecimation(double sourceRate, double targetRate);

// Bước chuẩn bị tín hiệu: trộn về mono ngay trong `audio.samples` (không cấp phát bản sao ở tần
// số gốc), hạ mẫu theo hệ số nguyên (Decimator polyphase) từng khối ~1 giây, rồi giải phóng
// `audio.samples`. Bộ nhớ đỉnh ≈ PCM đã giải mã + tín hiệu ra (bài 4 phút stereo 48 kHz ≈ 57 MB).
// Bộ lọc trễ ~1 ms (48 mẫu gốc) — không đáng kể với tone/BPM nên không bù.
//
// onProgress(0..1) được gọi sau mỗi khối; trả false để huỷ (hàm trả kResultCancelled).
tc_result prepareAnalysisSignal(DecodedAudio& audio, double targetRate,
                                const std::function<bool(float)>& onProgress, AnalysisSignal& out);

}  // namespace tonecore
