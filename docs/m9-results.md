# Kết quả M9

Số đo của M9 (kế hoạch: `docs/m9-plan.md`). Phần M9a đo trên Mac. Số đo trên iPhone (M9b) và Android
(M9c) thêm vào sau.

## B1 — trạng thái phát khi vừa nạp bài (vấn đề #11), engine 0.9.1

Test `BeatPlayerLoad.ConcurrentLoadsNeverShowOldPosition`:
- Một luồng âm thanh chạy liên tục.
- Đổi beat 2000 lần, mỗi lần đúng lúc beat cũ đang phát.
- Sau mỗi lần đổi, đọc vị trí phát 200 lần.

| Mã | Số lần đổi beat mà còn đọc thấy vị trí của beat cũ |
|---|---|
| Trước khi sửa (0.9.0) | 30 / 2000 |
| Sau khi sửa (0.9.1) | **0 / 2000**; sạch ThreadSanitizer |

## B3 — "hát 30 phút" (phần DSP)

Test `RealTime.LongSessionSoak` (`TC_SOAK_MINUTES=30`), bản Release, Mac M1, khối 256 mẫu.

Đầu vào:
- giọng giả (6 thanh, rung, lướt nốt, câu to, khoảng lặng có hơi thở / "s");
- beat hợp âm to;
- bật chỉnh giọng.

Thao tác trong lúc chạy:
- đổi thể loại mỗi 30 s;
- đổi mức chỉnh giọng mỗi phút (có lúc tắt);
- đổi tone beat mỗi 2 phút;
- tua mỗi 90 s;
- hết bài thì phát lại.

| Chỉ số | Giá trị |
|---|---|
| Thời gian chạy 30 phút âm thanh | 24,5 s |
| CPU trung bình / p99 (cả engine) | **1,34% / 2,08% một nhân** (chỉ tiêu mục 7: ≤ 25% trên máy tầm trung) |
| Trung vị thời gian mỗi khối: 5 phút đầu / 5 phút cuối | 72,0 / 73,0 µs — không chậm dần |
| Cấp phát trong audio callback | 0 |
| Mẫu NaN / Inf / vượt 0,99 | 0 (đỉnh 0,982; trần limiter 0,98, cho phép vượt < 1%) |
| Phát lại khi hết bài | 19 lần |

CI chạy bản 30 phút ở job `bench` (Linux), kết quả trong artifact `bench-linux/soak.txt`. Mọi lần chạy
test khác chạy bản 1 phút (Debug 7,5 s).

Phần này không thay được bài thử thật: xrun do I/O (luồng âm thanh của hệ điều hành bị trễ) chỉ đo
được khi hát 30 phút trên máy thật (M9b / M9c).

## B4 — "Kiểm tra máy" (Cài đặt → Nâng cao)

Cách chạy:
- Engine phụ không có thiết bị âm thanh, chạy ở isolate riêng.
- Giọng giả: giai điệu La thứ, rung, vài nốt lệch +35 cent, khoảng lặng.
- Beat giả, chỉnh giọng Ca sĩ, preset Bolero, nghe lại giọng, limiter.
- Đo thời gian từng lời gọi engine (khối 256 mẫu, bộ đệm native dùng lại).

| Máy | Bản | Âm thanh | CPU trung bình | p99 | Cao nhất | Thời gian chạy |
|---|---|---|---|---|---|---|
| Mac M1 (8 GB, đang chạy test khác) | engine Release, Dart JIT (`flutter test`) | 30 phút | **2,0%** | 11,4% | 1396% (1 khối 74 ms) | 42 s |

- **Đánh giá theo CPU trung bình** (chỉ tiêu mục 7: ≤ 25%). p99 và cao nhất ghi để tham khảo.
- Luồng chạy thử không có ưu tiên real-time như luồng âm thanh thật, nên bị hệ điều hành chen
  ngang. Test C++ trên cùng máy có p99 chỉ 2,1%.

Bảng 13 máy (M9b / M9c) điền ở đây từ file người thử gửi về (nút "Gửi kết quả").
