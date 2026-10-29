# ADR-006: Tách giọng ca sĩ trên server — giao thức theo đoạn, quyền riêng tư

- Trạng thái: Đã chấp nhận (2026-10-29 duyệt cùng kế hoạch M6; giao thức theo đoạn chốt sau B1;
  cài đặt server ở B3)
- Liên quan: `docs/m6-plan.md`, CLAUDE.md mục 4 / 7 / 11, ADR-001 (C API)

## Bối cảnh

Spec M6: tách giọng trên server (FastAPI + Demucs), bài 4 phút ≤ 60 s, giới hạn 10 phút / bài, tự
xoá sau 24 giờ, chỉ upload khi người dùng đồng ý. Server không được dùng ffmpeg / libsndfile (LGPL).
Đo ở B1: htdemucs trên Mac M1 tách bài 4 phút mất 32–42 s — nếu upload xong mới tách thì tổng thời
gian sát 60 s.

Trọng số htdemucs "chỉ cho mục đích khoa học" (issue facebookresearch/demucs#327) → chỉ dùng phát
triển nội bộ; bộ tách phải thay được.

## Quyết định

1. **Máy người dùng giải mã** bằng codec OS và nén **FLAC 16-bit** bằng bộ nén tự viết theo RFC 9639
   (`tc_exporter_*`, C API 0.7) → server không cần thư viện giải mã nào ngoài `tcflac` của engine
   (dr_flac). Tần số 44,1 kHz (tần số của model).
2. **Giao thức theo đoạn** (`server/src/chuantone_server/app.py`):
   - `POST /v1/jobs` khai báo tần số, số kênh, tổng số mẫu, số mẫu mỗi đoạn (~20 s), số đoạn.
   - `PUT /v1/jobs/{id}/chunks/{i}`: mỗi đoạn là một file FLAC độc lập (gửi lại / lộn thứ tự được).
   - Server tách theo **cửa sổ 30 s chồng 2 s** ngay khi phần đầu bài liền mạch đủ dài; vùng chồng
     trộn tuyến tính. Upload, tách và tải về chạy gối nhau.
   - `GET /v1/jobs/{id}/vocals/{i}`: giọng của đúng đoạn i (cùng ranh giới mẫu) → app tải dần.
   - `DELETE /v1/jobs/{id}` khi tải xong.
3. **Chỉ trả stem giọng.** Phần nhạc = bài gốc − giọng, tính trên máy (đúng cách "no_vocals" của
   Demucs); engine phát `phần nhạc + k · giọng` với k = "Giữ giọng ca sĩ gốc" (`TC_NODE_VOCAL`,
   mặc định 0). Đỡ một nửa dữ liệu tải về, không lưu thêm file nhạc (`tc_load_beat_with_vocals`
   tính lại khi nạp bài).
   - Đổi tone: phần nhạc và giọng đổi **riêng** (hai làn của bộ render, cùng khối) rồi mới cộng —
     đổi tone cả bài rồi trừ giọng đã đổi tone sẽ không triệt tiêu (bộ đổi tone phi tuyến).
4. **Quyền riêng tư:**
   - Chỉ gửi khi người dùng bấm "Tách giọng ca sĩ" và đồng ý.
   - Server không nhận tên bài; không ghi log nội dung âm thanh.
   - Mọi file xoá sau khi tải xong (DELETE) hoặc tối đa 24 giờ; job bỏ dở 15 phút thì xoá.
   - Thư mục mồ côi xoá khi khởi động.
5. **Chống lạm dụng:** mã máy ngẫu nhiên (`X-ChuanTone-Device`) — chỉ máy tạo job mới thấy job;
   giới hạn 50 job / máy / ngày; đoạn ≤ 32 MB; bài ≤ 10 phút; khoá API tuỳ chọn
   (`CHUANTONE_API_KEY`). HTTPS khi lên máy chủ thật (M9).
6. **Hàng đợi trong tiến trình**: một luồng tách, chọn cửa sổ sẵn sàng của job tạo sớm nhất; trạng
   thái job trong bộ nhớ (server khởi động lại → job mất, app báo lỗi và cho thử lại). arq + Valkey
   khi chạy nhiều máy (M9).

## Hệ quả

- Bài 4 phút trên Mac M1, đo bằng `server/tools/client_sim.py`: tổng 42–45 s với mạng không giới
  hạn / 20 / 8 Mbit/s. Upload được giấu sau phần tách (`docs/separation-results.md`).
- Ghép cửa sổ đúng: với bộ tách tuyến tính, kết quả theo cửa sổ khớp tách một lần (test).
- Đổi bộ tách (API trả phí, model khác) chỉ thay lớp `Separator`; giao thức với app giữ nguyên.
- Mất job khi server khởi động lại — chấp nhận ở M6 (một máy dev).
