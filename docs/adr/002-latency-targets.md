# ADR-002: Chỉ tiêu độ trễ khi nghe lại giọng

- Trạng thái: Đã chấp nhận theo kế hoạch đã duyệt (2026-10-20); chỉ tiêu Android tầm trung sẽ chốt lại sau khi đo ở M1.

## Bối cảnh
Spec mục 7 yêu cầu round-trip iOS ≤ 15 ms, Android flagship ≤ 20 ms, tầm trung ≤ 30 ms. Mục 3.4 lại cho riêng khối chỉnh giọng tới 15 ms. Phần cứng iOS với buffer 128 frame đã tốn khoảng 8–12 ms, nên khi bật chỉnh giọng không thể giữ tổng ≤ 15 ms.

TD-PSOLA cần nhìn trước khoảng một chu kỳ của f0 thấp nhất cần chỉnh: 10 ms tương ứng f0 = 100 Hz.

## Quyết định

| Đường tín hiệu | iOS | Android flagship | Android tầm trung |
|---|---|---|---|
| Tắt chỉnh giọng (chỉ EQ/comp/reverb) | ≤ 15 ms | ≤ 20 ms | ≤ 30 ms (chốt ở M1) |
| Bật chỉnh giọng | ≤ 25 ms | ≤ 30 ms | ≤ 40 ms |

- Khối pitch có **độ trễ cố định 10 ms**, không đổi theo f0 (độ trễ thay đổi sẽ gây méo). Giọng dưới 100 Hz dùng grain cắt ngắn, chấp nhận giảm chất lượng nhẹ.
- Limiter đầu ra lookahead tối đa 1 ms.
- **Chốt ở M2 (2026-10-21):** khi TẮT chỉnh giọng, giọng đi đường thẳng **không trễ** (bỏ hẳn 10 ms). Bật/tắt chuyển dần giữa đường khô (không trễ) và đường ướt (trễ 10 ms) trong **20 ms** để không có tiếng click. `tc_metrics.algorithm_latency_frames` báo 0 hoặc 480.
- Đo trên iPhone 13 Pro (M1): round-trip 12,6 ms qua loa ngoài → khi bật chỉnh giọng dự kiến ≈ 22,6 ms (chỉ tiêu ≤ 25 ms).

## Phương án dự phòng
Máy không đạt chỉ tiêu → app gợi ý chế độ **Soundcard OTG** (nghe lại bằng phần cứng của soundcard, app chỉ xử lý bản thu) và cảnh báo rõ trên UI.
