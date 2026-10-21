# Rà soát bằng sáng chế (bản nháp – cần luật sư xem trước khi phát hành)

Tài liệu này sẽ hoàn thiện dần theo milestone. Mỗi mục: số bằng, phạm vi liên quan, cách ToneCore triển khai, khác biệt. **Đây không phải ý kiến pháp lý.**

| Bằng / chủ đề | Liên quan tới | Trạng thái rà soát |
|---|---|---|
| US10930256B2 (chỉnh cao độ real-time trong karaoke trên di động) | Chỉnh giọng M2 | Cần luật sư đọc claims |
| Các bằng về chỉnh cao độ của Antares (vd. US5973252) | Chỉnh giọng M2 | Cần xác minh tình trạng hết hạn |
| Bằng về tìm kiếm bằng fingerprint chòm sao đỉnh phổ (Wang 2003) | Fingerprint bài hát | Cần xác minh tình trạng hết hạn |
| PSOLA (France Télécom/CNET) | Pitch shifter M2 | Cần xác minh tình trạng hết hạn |

## M2 — Dò cao độ & tự chỉnh giọng (mô tả để luật sư so với claims)

Mã nguồn: `engine/src/pitch/`. Viết từ tài liệu công khai (xem `docs/references.md`), không tham khảo mã hay sản phẩm của bên khác.

1. **Dò cao độ**: McLeod Pitch Method (bài báo ICMC 2005) trên tín hiệu hạ mẫu ~16 kHz, cửa sổ 40 ms, hop 4 ms; theo dõi f0 bằng ngưỡng clarity hai mức, chặn bước nhảy > 700 cent bằng xác nhận 3 khung, lọc trung vị 3 khung.
2. **Chọn nốt đích**: người dùng chọn tone bằng tay (chủ âm + thang âm); cao độ được lượng tử về nốt gần nhất thuộc thang, có vùng trễ (hysteresis) ở ranh giới hai nốt.
3. **Đường cong chỉnh**: chia giọng thành nốt (theo khoảng cách tới trung bình nốt), tâm nốt = trung bình trượt ≤ 400 ms bỏ 48 ms đầu nốt; lượng chỉnh = nốt đích − (p·tâm nốt + (1−p)·cao độ tức thời), làm mượt bậc 2 theo `retune`, chậm dần trên nốt dài (`humanize`), bỏ qua lệch nhỏ (`flex`), kẹp ≤ 200 cent.
4. **Dịch cao độ**: TD-PSOLA (Moulines & Charpentier 1990) với độ trễ cố định 10 ms; mốc chu kỳ đặt theo f0 rồi tinh chỉnh bằng tương quan chuẩn hoá với chu kỳ trước; chồng hạt Hann có chuẩn hoá theo tổng cửa sổ; đoạn vô thanh đi thẳng.
5. **Ứng dụng karaoke trên di động**: chạy real-time trong audio callback, nghe lại qua tai nghe; tone chọn tay ở M2 (tự dò tone ở M3).

Điểm cần luật sư xác minh:
- **US10930256B2**: so các claim độc lập với mục 1–5 ở trên (đặc biệt cách xác định nốt đích và cách áp dịch cao độ trong luồng karaoke).
- **US5973252 (Antares)**: nộp đơn năm 1998 → theo thời hạn 20 năm có thể đã hết hiệu lực; cần xác minh trạng thái chính thức.
- **PSOLA (France Télécom/CNET, cuối thập niên 1980)**: có thể đã hết hạn; cần xác minh số bằng và trạng thái.

Tên tính năng trong app là "Tự chỉnh giọng" (không dùng "Auto-Tune").
