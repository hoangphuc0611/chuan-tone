# ADR-007: Thu âm đa track — ghi thô, bù trễ lúc đọc, căn hình – tiếng theo đồng hồ máy

- Trạng thái: Đã chấp nhận (2026-11-02 duyệt cùng kế hoạch M7; bộ thu cài ở B1)
- Liên quan: `docs/m7-plan.md`, CLAUDE.md mục 3.1 / 3.8 / 3.9 / 11, ADR-001 (C API), ADR-002
  (độ trễ), ADR-005 (chuỗi FX)

## Bối cảnh

Spec mục 3.8: ghi đồng thời giọng khô, giọng đã xử lý và beat (WAV 48 kHz), bù độ trễ đo được để
giọng khớp nhạc, cho "render lại" preset khác lên giọng khô. M7 còn cần video 9:16 có camera trước,
đăng TikTok, hình khớp tiếng.

Ràng buộc: audio thread không cấp phát / khoá / I/O (mục 0.4). Độ trễ khứ hồi khác nhau theo máy và
đường ra (loa ngoài ~13 ms, tai nghe có dây, Bluetooth ~200 ms), có thể đo lại sau khi đã thu.

## Quyết định

1. **Ghi thô ba track, cùng frame đầu, dài bằng nhau** (`record/recorder.*`, C API 0.8
   `tc_record_start / stop / get_status`):
   - `dry.wav`: micro thô trước mọi xử lý (lọc thông cao, cổng, chỉnh giọng), mono float32 — giữ
     nguyên độ phân giải để render lại;
   - `wet.wav`: giọng sau chỉnh giọng + FX, trước gain nghe lại và trước khi trộn, stereo 16-bit;
   - `beat.wav`: beat đang phát (đã đổi tone, kèm giọng ca sĩ theo `TC_NODE_VOCAL`), trước gain
     beat và limiter, stereo 16-bit.
   Thu liên tục kể cả khi beat dừng / tua; `start_beat_frame` cho biết beat đang ở đâu lúc bắt đầu.
2. **Đường đi dữ liệu:** audio thread xen kẽ 5 kênh vào buffer cấp sẵn rồi đẩy nguyên khối vào ring
   SPSC ~4 s (cấp phát ở lần thu đầu, giữ tới khi huỷ engine). Không đủ chỗ → bỏ cả khối, đếm
   `dropped_frames` (5 kênh cùng rơi nên các track vẫn khớp nhau); không bao giờ chờ. Luồng ghi riêng
   rút ring ~10 ms một lần, ghi qua tên tạm `.part`, sửa header rồi đổi tên khi xong — ba file cùng
   chốt hoặc cùng huỷ.
3. **Dừng:** trạng thái atomic `Armed → Recording → StopRequested → Draining → Done / Failed`. Audio
   thread xác nhận dừng ở block kế tiếp; không có callback (engine dừng, bị cuộc gọi ngắt) thì sau
   1 s tự chốt. `tc_engine_stop` / `tc_engine_destroy` cũng chốt file. Đang thu thì không đo độ trễ
   và ngược lại (lúc đo, engine chỉ phát tín hiệu đo).
4. **Bù độ trễ lúc đọc, không lúc ghi.** File không bị dịch; app ghi độ lệch vào `take.json`. Với R
   = độ trễ khứ hồi, L = `output_lookahead_frames` (limiter đầu ra nhìn trước 1 ms), P =
   `pitch_latency_frames` (khối chỉnh giọng 10 ms khi bật):
   - beat frame m được nghe sau L + độ trễ ra; người hát theo nó, tiếng vào micro sau thêm độ trễ
     vào → **giọng khô frame n khớp beat frame n − (R + L)**;
   - giọng đã xử lý khớp beat frame n − (R + L + P).
   Nhờ đó đo lại độ trễ hoặc chỉnh tay "Giọng sớm / muộn ±" sau khi thu vẫn đúng; render lại chỉ
   cần một con số.
5. **Đồng hồ máy cho camera:** backend chuyển cho engine đồng hồ máy (ns) lúc frame đầu của mỗi
   buffer ra thiết bị (iOS: `AudioTimeStamp.mHostTime` của render callback RemoteIO, đổi bằng
   `mach_timebase_info`; Android: 0 ở M7). Bộ thu lưu mốc của frame đầu tiên
   (`start_host_time_ns`) cùng `output_latency_ms` hệ điều hành báo. Khung hình camera iOS mang mốc
   theo cùng đồng hồ máy → lúc ghép video: lệch = (mốc khung đầu) − (mốc beat frame đầu được nghe =
   `start_host_time_ns` + độ trễ ra + L). Hằng số còn thiếu (nếu có) đo bằng vỗ tay ở B6.

6. **Nghe lại bằng chính chuỗi real-time** (`tc_load_take`, B3a): beat.wav làm beat, giọng khô đã
   dịch sớm (R + L, ± chỉnh tay) nằm trong Track như một "làn giọng"; mỗi block BeatPlayer xuất
   giọng (đọc sớm thêm P để bù khối chỉnh giọng) và engine đưa nó vào chuỗi giọng thay cho micro.
   Đổi preset / mức chỉnh giọng dùng lại đường đổi không click của M5 → nghe ngay, không chờ render.
   Không đổi tone (beat.wav đã ở tone lúc hát).

7. **Rời màn Nghe lại thì gỡ bản thu** (`tc_unload_beat`): chế độ nghe lại thay micro bằng giọng bản
   thu chừng nào bản thu còn nạp → phải gỡ khi đóng màn, kẻo màn khác (Phòng thử chưa chọn bài) mất
   nghe lại giọng.
8. **Video camera** (B6): AVCaptureSession chỉ có hình, không tự cấu hình audio session; khung hình
   mang mốc theo đồng hồ của phiên (đồng hồ máy). Ghép: khung đầu đặt ở (mốc khung đầu) −
   (`start_host_time_ns` + `output_latency_ms` + L) − (điểm cắt đầu) trên trục file tiếng đã xuất,
   cộng hằng hiệu chỉnh `cameraSyncNs` (đo bằng vỗ tay, `tools/video/av_sync.swift`).

## Hệ quả

- Bản thu 4 phút ≈ 140 MB (WAV như spec); app hiện dung lượng và cho xoá; FLAC để sau nếu cần.
- Audio thread chỉ thêm một lần chép micro (khi đang thu) và một lần xen kẽ 5 kênh — không cấp phát
  (`RealTime.RecordingDoesNotAllocate`).
- Định dạng bản thu không phụ thuộc độ trễ đo được → đổi cách đo / hiệu chỉnh không làm hỏng bản
  thu cũ.
- `BackendHost::processAudio` thêm tham số đồng hồ máy (nội bộ, không đổi C API).
