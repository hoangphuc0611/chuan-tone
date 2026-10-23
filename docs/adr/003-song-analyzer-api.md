# ADR-003: Phân tích bài là handle riêng, kết quả theo kiểu kéo

- Trạng thái: Đã chấp nhận (2026-10-22 duyệt cùng kế hoạch M3; cài đặt 2026-10-23, C API 0.4)
- Thay thế điểm 4 của ADR-001 cho việc dài: không dùng `NativeCallable.listener`.

## Bối cảnh
Dò tone / BPM / LUFS cho cả bài mất ~1 s (máy mạnh) tới vài giây (máy yếu) và phải chạy ngoài luồng
UI. Spec (CLAUDE.md 3.9) gợi ý `tc_analyze_file(path, callback)`. ADR-001 dự kiến dùng
`NativeCallable.listener` của Dart cho các việc dài.

## Quyết định
1. **Handle riêng `tc_analyzer`**, không gắn với `tc_engine`:
   `tc_analyzer_create / start / get_status / cancel / get_result / get_key_timeline / destroy`.
   Mỗi analyzer có một luồng nền (Worker) riêng.
2. **Kiểu kéo**: UI gọi `tc_analyzer_get_status` (~10 lần/giây) lấy trạng thái, giai đoạn, tiến độ
   0–1; xong thì đọc `tc_analyzer_get_result` + `tc_analyzer_get_key_timeline`. Không có callback
   gọi ngược vào Dart.
3. **Huỷ**: `tc_analyzer_cancel` chỉ bật cờ (không chờ); `tc_analyzer_destroy` bật cờ rồi chờ luồng
   nền dừng (các bước phân tích kiểm tra cờ mỗi khối ~1 s audio → dừng trong vài chục ms).
   Mã lỗi mới `TC_ERR_CANCELLED`.
4. **Kết quả đưa vào engine** bằng hai hàm mới: `tc_set_tuning(cents)` (lưới nốt của chỉnh giọng
   theo tuning của beat) và `tc_set_key_timeline(segments, count)` (engine tự đổi key theo vị trí
   beat, phát `TC_EVENT_KEY_CHANGED`; `tc_set_key` tắt chế độ này).

## Lý do
- Không phải quản lý vòng đời callback: nếu isolate Dart bị huỷ trong lúc phân tích, callback
  `NativeCallable` gọi vào bộ nhớ đã mất → crash. Kiểu kéo không có rủi ro này.
- Chạy được khi engine audio chưa start (lúc nhập thư viện bài, trước khi mở màn hát).
- Cùng mã dùng cho công cụ dòng lệnh (`keylab`) và test C++.
- Nhất quán với `tc_poll_events` / `tc_get_metrics` sẵn có.

## Hệ quả
- UI cần một timer kéo trạng thái khi đang phân tích (đã có sẵn nhịp kéo cho đồng hồ cao độ).
- Mỗi hàm C mới có test trong `engine/tests/unit/analyzer_api_test.cpp` /
  `engine_m3_test.cpp`; test Dart thêm ở M3 bước B8 (binding 0.4).
- Key timeline chuyển sang audio thread qua hàng đợi SPSC + key override chỉ audio thread ghi →
  không cấp phát, không khoá trong callback (test `RealTime.KeyTimelineDoesNotAllocate`).
