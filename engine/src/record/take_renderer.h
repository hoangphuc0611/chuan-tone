#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "fx/fx_presets.h"
#include "pitch/presets.h"
#include "pitch/scales.h"
#include "tonecore/tonecore.h"

namespace tonecore {

// Thông số render lại một bản thu (M7, docs/adr/007-recording-sync.md). Vị trí tính bằng frame
// của bản thu (tần số dry.wav).
struct TakeRenderSettings {
  std::string beatPath;   // beat.wav
  std::string voicePath;  // dry.wav
  std::string outPath;
  int64_t voiceOffsetFrames = 0;  // như tc_take_config
  uint64_t startFrame = 0;        // cắt đầu
  uint64_t endFrame = 0;          // cắt đuôi (0 = hết bài)

  FxPreset fx;
  bool fxEnabled = true;
  float fxWet = 1.0f;  // "Vang"
  float tempoBpm = 100.0f;

  bool correctionEnabled = false;
  CorrectionPreset correction = CorrectionPreset::Natural;
  int keyRoot = 9;
  Scale keyScale = Scale::NaturalMinor;
  float tuningCents = 0.0f;
  struct Segment {
    double startMs;  // tính từ đầu bản thu
    int root;
    Scale scale;
  };
  std::vector<Segment> timeline;  // tăng dần; rỗng = dùng keyRoot / keyScale

  bool gateEnabled = false;
  float gateThresholdDb = -50.0f;

  float voiceGain = 1.0f;
  float beatGain = 1.0f;

  bool normalize = true;
  float targetLufs = -14.0f;
  float truePeakDb = -1.0f;

  int32_t format = TC_RENDER_WAV16;  // tc_render_format
  int32_t aacBitrate = 192000;
};

// Tiến độ / kết quả (atomic: luồng render ghi, UI đọc).
struct TakeRenderProgress {
  std::atomic<bool> cancelled{false};
  std::atomic<float> progress{0.0f};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint32_t> sampleRate{0};
  std::atomic<float> mixLufs{0.0f};     // độ to bản trộn trước chuẩn hoá (NaN = im)
  std::atomic<float> outputLufs{0.0f};  // độ to file xuất
  std::atomic<float> truePeakDb{0.0f};  // đỉnh thật file xuất (dBTP, trước khi nén AAC)
  std::atomic<float> gainDb{0.0f};      // gain chuẩn hoá đã áp
};

// Render (chạy ở luồng nền, có thể vài giây):
//  1. Giọng khô (đã dịch sớm voiceOffsetFrames, đọc sớm thêm độ trễ khối chỉnh giọng) → cổng →
//     chỉnh giọng (key / timeline) → FX → trộn với beat (voiceGain / beatGain), fade 10 ms ở chỗ
//     cắt; chuỗi giọng chạy trước điểm cắt đầu 2 s để trạng thái (vang, dò cao độ) liền mạch.
//     Bản trộn float ghi ra file tạm, đồng thời đo độ to (BS.1770).
//  2. Gain về targetLufs (kẹp −30…+24 dB) → limiter đỉnh thật ≤ truePeakDb → ghi file đích.
// Kết quả: TC_OK / mã lỗi (TC_ERR_CANCELLED khi huỷ). File đích ghi qua tên tạm.
tc_result renderTake(const TakeRenderSettings& s, TakeRenderProgress& progress);

}  // namespace tonecore
