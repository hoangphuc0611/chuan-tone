# Kết quả đo độ trễ round-trip

Cập nhật: 2026-10-21 · 6 lần đo · 2 cấu hình máy.

Đo bằng màn "Đo độ trễ" của app (chuỗi MLS, tương quan chéo, trung vị 5 lần). Chỉ tiêu theo `docs/adr/002-latency-targets.md` cho trường hợp **tắt chỉnh giọng**: iOS ≤ 15 ms, Android flagship ≤ 20 ms, tầm trung ≤ 30 ms. Khi bật chỉnh giọng cộng thêm 10 ms. Từ M5, limiter đầu ra cộng thêm 1 ms cho mọi trường hợp (phép đo MLS không đi qua limiter: trễ nghe lại thật = số đo + `tc_metrics.algorithm_latency_frames`).

Lưu ý: đo bằng loa ngoài → micro khác đường tai nghe; số chính xác nhất cho đường tai nghe là đo bằng dongle loopback.

| Máy | HĐH | Đường | API | Buffer (burst) | Số lần | Trễ đo (trung vị) | Thấp–cao | HĐH báo | Đánh giá |
|---|---|---|---|---|---|---|---|---|---|
| Apple iPhone14,2 | iOS 26.6.2 | Bluetooth | remoteio | 128 (128) | 3 | **212.6 ms** | 211.0–215.1 | 177.9 ms | Không áp dụng (Bluetooth) |
| Apple iPhone14,2 | iOS 26.6.2 | Loa ngoài | remoteio | 128 (128) | 3 | **12.6 ms** | 12.6–12.7 | 4.0 ms | Đạt |
