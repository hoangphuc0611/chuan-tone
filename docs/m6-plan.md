# M6 – Tách vocal (server) + màn Phân tích bài

> Kế hoạch đã duyệt 2026-10-29 (điểm 1: htdemucs chỉ phát triển nội bộ; điểm 2: server trên Mac;
> điểm 3–8: theo đề nghị). Điều kiện hoàn thành (CLAUDE.md mục 9): **bài 4 phút xử lý
> ≤ 60 giây**. Spec liên quan: mục 4 (tách vocal), mục 5 màn 2 (Phân tích bài), mục 7 (quyền riêng
> tư), mục 11 (upload FLAC).

## Tiến độ

- [~] **B1** (2026-10-29, chờ bạn quyết đi tiếp):
  - `server/` (uv, Python 3.13): lớp bộ tách (`fake` cho test, `htdemucs` qua API của gói demucs).
  - Gói demucs / openunmix cài không kéo phụ thuộc; tqdm / dora.log / omegaconf thay bằng mô-đun
    rỗng; mọi gói còn lại MIT / BSD / Apache-2.0 (+ PSF-2.0, CC0, BSL-1.0 — đều thoáng).
  - `tools/bench_separate.py` (thời gian, SDR khi trộn giọng đã biết).
  - Số đo trên Mac M1 8 GB, bài 4 phút: mặc định demucs 62–97 s; **GPU Apple + autocast 16-bit +
    chồng đoạn 0,1: 31,8–41,8 s** (54–84 s khi máy thiếu RAM), SDR không đổi (11,5 dB) →
    `docs/separation-results.md`.
- [ ] B2 … B7: xem mục 7.

---

## Các điểm cần quyết (2026-10-29: duyệt theo đề nghị)

1. **Model tách giọng — vướng giấy phép (phải hỏi theo CLAUDE.md mục 0.2).**
   - **htdemucs** (Meta, Demucs v4) tách tốt nhất trong các model công khai. Mã nguồn MIT, nhưng
     người duy trì viết (issue #327, 23/5/2022): *"The model weights are not covered by the MIT
     license, and are provided only for scientific purposes."* Trọng số học trên MUSDB18-HQ (phần
     lớn bài CC BY-NC-SA) + dữ liệu nội bộ. Không tìm thấy giấy phép thương mại nào thay thế.
   - Các model mở khác cũng không sạch:
     - Spleeter (Deezer): mã MIT, trọng số không ghi giấy phép riêng, tách kém rõ so với htdemucs.
     - Open-Unmix: trọng số học trên MUSDB18 (phi thương mại).
     - Model cộng đồng UVR / RoFormer: phần lớn không ghi giấy phép.
   - Phương án:
     - **(a) htdemucs chỉ để phát triển nội bộ.** Server chạy trên Mac của bạn, không phát hành,
       không đưa trọng số vào app. Trước khi phát hành chọn (b), xin giấy phép Meta, hoặc (d).
       - Rủi ro: "chỉ cho mục đích khoa học" — làm mẫu thử cho sản phẩm thương mại là vùng xám
         (thấp vì không phân phối, nhưng không bằng 0). Bạn quyết.
     - **(b) API tách giọng trả phí có giấy phép**, ví dụ LALAL.AI: khoảng $0,06–0,07 / phút, tức
       ≈ 7.000 đ cho bài 4 phút (cần lấy báo giá chính thức); AudioShake tính theo phút.
       - Không cần GPU, không vướng trọng số.
       - Nhưng: âm thanh đi qua bên thứ ba (lời xin đồng ý phải ghi rõ), tốn tiền theo bài, phụ
         thuộc nhà cung cấp. Server mình thành lớp trung gian (giấu khoá API, giới hạn, xoá sau
         24 giờ).
     - **(c) Spleeter**: chất lượng thấp hơn; vẫn phải hỏi Deezer về trọng số.
     - **(d) Tự huấn luyện model** trên dữ liệu có giấy phép: để giai đoạn sau, cần mua hoặc thu
       dữ liệu.
   - **Đề nghị:**
     - Server có **lớp "bộ tách" thay được**: trong CI dùng bộ giả.
     - M6 phát triển và đo bằng (a) trên Mac, ghi rõ trong `docs/licenses.md` là "chỉ phát triển,
       cấm phát hành".
     - Chốt (b), giấy phép Meta hoặc (d) **trước khi phát hành** (thêm vào danh mục M9).
     - Nếu bạn không muốn chạm vào vùng xám: làm (b) ngay ở M6 (cần bạn tạo tài khoản API).
   - Kèm theo (a) — thư viện của gói `demucs`:
     - cài `--no-deps` để không kéo `lameenc` (bọc LAME, **LGPL**), không dùng ffmpeg /
       torchaudio để đọc file;
     - các phụ thuộc còn lại: torch (BSD-3), einops (MIT), julius (MIT), dora-search (MIT),
       pyyaml (MIT);
     - **tqdm là MIT + MPL-2.0** (MPL ngoài danh sách cho phép): đề nghị thay bằng một mô-đun
       rỗng cùng tên trong server (demucs chỉ dùng nó để in tiến độ), không cài tqdm.
2. **Server chạy ở đâu trong M6.**
   - Đề nghị: **trên Mac của bạn (M1)**, iPhone gọi qua Wi-Fi nhà (`http://192.168.x.x:8000`).
     - Miễn phí.
     - Bản build thử cần quyền "Mạng cục bộ" của iOS (`NSLocalNetworkUsageDescription`) và ngoại
       lệ ATS cho mạng cục bộ — chỉ bản phát triển.
     - Địa chỉ server nhập tay ở Phòng thử.
   - Kèm `Dockerfile` để sau này lên máy chủ GPU (ước T4: ~10–15 s / bài 4 phút; chi phí và nhà
     cung cấp chốt ở M9).
   - Nếu Mac M1 tách chậm (đo ở B1): 60 s đo trên GPU thuê theo giờ — cần bạn có tài khoản.
3. **Định dạng gửi / nhận.**
   - Gửi lên:
     - Máy giải mã bằng codec của OS (như phát beat) rồi nén **FLAC 16-bit** (libFLAC, BSD-3, vào
       engine) — đúng hướng đã chốt ở mục 11.
     - Bài 4 phút 44,1 kHz stereo ≈ 42 MB PCM → **≈ 25 MB FLAC**. Wi-Fi 20–50 Mbit/s mất 4–10 s;
       4G 5–15 Mbit/s mất 15–40 s.
   - Nhận về:
     - **chỉ stem giọng** (FLAC). Phần nhạc **tự tính trên máy = bài gốc − giọng** — đúng bằng
       cách "no_vocals" của Demucs, mà đỡ tải một nửa.
     - Engine phát `bài gốc − (1 − k) · giọng`, với k = thanh "Giữ giọng ca sĩ gốc": không cần
       lưu thêm file nhạc.
   - Phương án khác:
     - gửi Opus 256 kbit/s (≈ 8 MB, nhanh hơn trên 4G) — cần thêm libopus (BSD-3) ở cả hai đầu;
     - nhận cả hai stem.
   - **Đề nghị**: FLAC + chỉ nhận giọng. Opus để dành nếu đo 4G không đạt.
4. **Khi nào tách.**
   - Đa số người dùng nạp **beat karaoke** (đã không có giọng) → không cần tách.
   - Đề nghị: màn Phân tích bài có nút **"Tách giọng ca sĩ"** (cho bài gốc có giọng). Lần đầu bấm
     hiện hộp xin đồng ý: gửi bài lên máy chủ ChuẩnTone, chỉ để tách giọng, tự xoá sau 24 giờ;
     có ô "Không hỏi lại".
   - Không tự gửi khi chưa bấm (mục 7: không upload khi chưa đồng ý).
   - Tự đoán "bài có giọng hay không" để gợi ý nút: để sau.
5. **"Giữ giọng ca sĩ gốc" và đổi tone.**
   - Engine phát thêm stem giọng, khớp từng mẫu với bài gốc, có gain riêng (thanh 0–100%,
     mặc định 0% = karaoke).
   - Đổi tone beat thì **render cả hai** (bài gốc và giọng) ở luồng nền: render cả bài ~2× thời
     gian (ước 5–6 s), nghe tone mới vẫn ≈ 0,1 s.
   - Giọng ca sĩ đổi tone bằng bộ đổi tone beat (không giữ formant) — chỉ là giọng mẫu để hát
     theo, chấp nhận.
   - **Đề nghị**: như trên.
6. **Dò tone trên phần nhạc đã tách.**
   - Phân tích bài gốc ngay (≈ 1 s, như bây giờ).
   - Tách xong thì **phân tích lại trên phần nhạc** (bài gốc − giọng): giọng ca sĩ hay luyến láy,
     làm nhiễu chroma.
   - So kết quả hai lần trên vài bài có giọng.
   - **Đề nghị**: có.
7. **Màn Phân tích bài** (mục 5 màn 2) làm thật ở M6, không nằm trong Phòng thử.
   - Lối vào: Trang chủ → nút to **"Thêm bài hát"** → chọn file → màn Phân tích bài.
   - Các bước có thanh tiến độ: (tách giọng) → dò tone → dò nhịp.
   - Kết quả: "Tone: La thứ (Am) – 92 BPM", chuyển tone nếu có, nút **sửa tone tay** (nhớ theo bài,
     thay cho tone dò được), nút **"Hát"** → hiện mở Phòng thử với bài đã nạp (Màn hát làm ở M8).
   - Thư viện bài (SQLite) và "bài gần đây" ở Trang chủ: để M8.
   - **Đề nghị**: như trên.
8. **Hàng đợi job trên server.**
   - Kế hoạch gốc: arq + Valkey. Với một máy, một bộ tách, đề nghị **hàng đợi trong tiến trình**
     (một worker, trạng thái job trong SQLite): ít thành phần, chạy được trên Mac.
   - Chuyển sang arq + Valkey khi chạy nhiều máy (M9).
   - **Đề nghị**: trong tiến trình.

---

## 1. Kết quả đầu ra

- **Server** `server/` (Python 3.12, uv, FastAPI):
  - `POST /v1/jobs` (nhận FLAC, kiểm tra ≤ 10 phút, ≤ 100 MB) → mã job;
  - `GET /v1/jobs/{id}` (hàng chờ / đang tách / xong / lỗi, tiến độ);
  - `GET /v1/jobs/{id}/vocals.flac`;
  - `DELETE /v1/jobs/{id}`.

  Ngoài ra:
  - Tự xoá mọi file sau khi tải xong hoặc tối đa 24 giờ.
  - Không ghi log nội dung âm thanh / tên bài.
  - Mã máy ngẫu nhiên + giới hạn số job mỗi máy mỗi ngày.
  - Lớp bộ tách: `htdemucs` (hoặc API trả phí, tuỳ điểm 1) và bộ giả cho test.
- **Engine 0.7:**
  - Xuất FLAC từ file bất kỳ — đọc bằng codec OS, nén bằng libFLAC — chạy luồng nền, handle kiểu
    kéo như `tc_analyzer`.
  - Nạp stem giọng đi kèm beat; gain "giữ giọng"; đổi tone render cả hai.
  - Phân tích trên "bài − giọng".
- **Binding 0.7 + app:**
  - Client upload / theo dõi / tải về (dart:io, không thêm thư viện), huỷ, thử lại;
  - lưu stem theo bài;
  - hộp xin đồng ý;
  - màn Phân tích bài; nút "Thêm bài hát";
  - thanh "Giữ giọng ca sĩ gốc";
  - ô địa chỉ server (bản phát triển).
- **CI:** job `server` (pytest với bộ tách giả, không cần torch).
- **Tài liệu:**
  - `docs/m6-plan.md`, `docs/separation-results.md` (số đo);
  - ADR-006 (luồng tách vocal, quyền riêng tư);
  - `docs/licenses.md` (libFLAC, các gói server, trạng thái trọng số model);
  - `docs/references.md`.

---

## 2. Luồng xử lý một bài

1. App: người dùng bấm "Tách giọng ca sĩ" → đồng ý → engine xuất FLAC (giải mã OS, giữ tần số
   gốc; 48 kHz thì đổi về 44,1 kHz cho model) → upload có thanh tiến độ.
2. Server: kiểm tra FLAC (đọc header, thời lượng) → xếp hàng → bộ tách → nén stem giọng FLAC →
   xoá file vào.
3. App: hỏi trạng thái mỗi 1 s → tải giọng → báo server xoá → lưu `songs/<khoá bài>/vocals.flac`.
4. Engine: nạp bài gốc + stem giọng (cùng tần số, cùng độ dài, khớp từng mẫu) → phân tích lại trên
   bài − giọng → app cập nhật tone / BPM / gợi ý preset.

Khớp mẫu:
- Stem giọng tính từ đúng PCM máy đã giải mã (FLAC không mất dữ liệu), nên `bài − giọng` triệt
  tiêu chính xác.
- Test "null": bài − giọng + giọng = bài, tới sai số 16-bit.
- Rủi ro: codec OS giải mã AAC ra độ dài / độ trễ đệm khác nhau giữa hai lần — test trên iPhone.

---

## 3. Ước lượng thời gian bài 4 phút (chỉ tiêu ≤ 60 s, đo ở B7)

| Bước | Wi-Fi + Mac M1 | Ghi chú |
|---|---|---|
| Giải mã + nén FLAC trên iPhone | 2–4 s | libFLAC mức 5 |
| Upload 25 MB | 4–10 s | 4G: 15–40 s |
| Tách (htdemucs) | **chưa biết — đo ở B1** | MPS / CPU M1; GPU T4 ước 10–15 s |
| Tải giọng ~15–20 MB | 3–8 s | |
| Phân tích lại | ~1 s | |

B1 là điểm dừng / đi tiếp: nếu tách trên Mac > 35 s cho bài 4 phút thì báo bạn trước khi làm tiếp.

---

## 4. Test và số đo

- Server (pytest, CI):
  - đủ các đường của API;
  - từ chối file > 10 phút, > 100 MB, không phải FLAC;
  - xoá sau tải / sau 24 giờ (giả thời gian);
  - hàng đợi nhiều job;
  - giới hạn theo máy;
  - huỷ giữa chừng.
- Engine:
  - FLAC xuất ra giải mã lại đúng từng mẫu;
  - null test bài − giọng;
  - phát bài + giọng khớp mẫu khi tua / đổi tone;
  - gain "giữ giọng" trượt mượt, không cấp phát trên audio thread;
  - phân tích trên bài − giọng.
- Chất lượng tách:
  - SDR giọng trên vài bài tự trộn từ giọng vocadito (CC BY 4.0) + beat dev;
  - bạn nghe 3–5 bài Việt có giọng;
  - dò tone trước / sau tách trên các bài đó.
- Thời gian end-to-end trên iPhone, bài 4 phút, tách từng bước như bảng mục 3.

---

## 5. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | Giấy phép trọng số model (điểm 1) | Lớp bộ tách thay được; ghi trạng thái trong `licenses.md`; chốt trước phát hành |
| 2 | Mac M1 tách chậm / 4G upload chậm → quá 60 s | Đo ở B1; GPU thuê; Opus thay FLAC; chỉ nhận stem giọng |
| 3 | Bài gốc và stem lệch mẫu (codec OS) | Stem tính từ đúng PCM đã giải mã; null test trên iPhone; nếu lệch thì app lưu PCM gốc dạng FLAC và phát từ đó |
| 4 | Bản quyền / quyền riêng tư khi upload bài có bản quyền | Chỉ khi người dùng bấm + đồng ý; xoá sau 24 giờ; không lưu, không chia sẻ; ghi vào `docs/patent-review.md` mục pháp lý để luật sư rà |
| 5 | Server bị lạm dụng (mở ra internet) | Mã máy + giới hạn mỗi ngày + giới hạn kích thước; HTTPS khi lên máy chủ thật |
| 6 | iOS treo app khi chuyển nền giữa lúc upload | M6: giữ màn hình mở + báo; upload nền bằng URLSession để M8 |

---

## 6. Ngoài phạm vi M6

- Tách trên máy (giai đoạn 2, mục 4).
- Thư viện bài SQLite, "bài gần đây".
- Tự đoán bài có giọng hay không.
- Upload nền.
- Server nhiều máy, HTTPS / tên miền.
- Kho tone dùng chung.

---

## 7. Các bước

| Bước | Nội dung | Ước (tuần) |
|---|---|---|
| B1 | Khung server + lớp bộ tách; đo htdemucs trên Mac (MPS / CPU), kiểm tra phụ thuộc → **dừng / đi tiếp** | 0,5 |
| B2 | Engine: libFLAC, xuất FLAC kiểu kéo (C API 0.7), test giải mã lại đúng mẫu | 0,5 |
| B3 | Server: API đủ, giới hạn, xoá 24 giờ, mã máy; pytest; CI `server`; Dockerfile | 0,5 |
| B4 | Engine: stem giọng đi kèm beat, gain "giữ giọng", đổi tone render cả hai, phân tích bài − giọng | 0,5 |
| B5 | Binding 0.7 + client trong app (upload / theo dõi / tải / huỷ / thử lại), lưu stem, hộp đồng ý, ô địa chỉ server | 0,5 |
| B6 | Màn Phân tích bài + "Thêm bài hát" + sửa tone tay + thanh "Giữ giọng ca sĩ gốc" | 0,5 |
| B7 | Đo end-to-end trên iPhone, nghe chất lượng tách, `docs/separation-results.md`, báo cáo M6 | 0,5 |

Tổng ≈ 3,5 tuần (kế hoạch gốc: 0,5 DSP + 4 mobile).

**Cách bạn tự kiểm tra khi xong M6:**
- Trên Mac: `cd server && uv run chuantone-server` (in địa chỉ để nhập vào app).
- Trên iPhone:
  - Trang chủ → "Thêm bài hát" → chọn một bài có giọng → "Tách giọng ca sĩ" → đồng ý.
  - Xem thời gian trên màn Phân tích bài (≤ 60 s với bài 4 phút).
  - Bấm "Hát" → kéo "Giữ giọng ca sĩ gốc" 0 → 100%, đổi tone ±2.
