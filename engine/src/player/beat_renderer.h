#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "core/worker.h"
#include "player/transposed_track.h"

namespace tonecore {

// Render một TransposedTrack trên luồng nền của engine (M4 B5, ADR-004).
//
// Thứ tự: bắt đầu từ khối chứa `fromFrame` (vị trí đang phát) chạy tới cuối bài, rồi lấp các khối
// còn thiếu từ đầu bài. Tua tới vùng chưa có (TransposedTrack::wantFrame) → mở lượt mới ngay ở đó.
// Mỗi bước chỉ render vài khối (~0,7 s audio, vài ms CPU) rồi tự đăng lại vào Worker → tua, đổi
// tone, nạp beat chen vào được ngay; `cancelled` được kiểm giữa các bước.
//
// Chỗ nối hai lượt: lượt bắt đầu ở khối b > 0 render trước kLeadInFrames mẫu ngay trước b (không
// công bố, giữ riêng). Khi một lượt khác đi tới sát b, đuôi khối b − 1 của nó được trộn dần
// (raised cosine) sang phần render trước đó → liền với khối b đã công bố, không "tách". Hai lượt
// bắt đầu ở hai chỗ khác nhau nên pha các thành phần ngân có thể lệch nhau; trộn trên ~21 ms.
class BeatRenderer {
 public:
  static constexpr uint32_t kBlocksPerStep = 8;
  static constexpr uint32_t kLeadInFrames = 1024;

  using ProgressFn = std::function<void(float progress)>;
  using DoneFn = std::function<void(int semitones, double renderMs)>;

  // Gọi trên luồng nền (Worker). `onProgress` / `onDone` cũng chạy trên luồng nền.
  static void start(Worker& worker, std::shared_ptr<TransposedTrack> track, uint64_t fromFrame,
                    double sampleRate, ProgressFn onProgress, DoneFn onDone);
};

}  // namespace tonecore
