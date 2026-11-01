# Kết quả tách giọng ca sĩ (M6)

Chỉ tiêu (CLAUDE.md mục 9): bài 4 phút xử lý ≤ 60 s (từ lúc bấm tới khi có stem giọng trên máy).
Kế hoạch: `docs/m6-plan.md`.

## B1 — tốc độ htdemucs trên Mac (2026-10-29)

Máy: MacBook Apple M1 (4 nhân hiệu năng + 4 nhân tiết kiệm), RAM 8 GB; torch 2.15, demucs 4.0.1,
Python 3.13. Bài: beat 4 phút 44,1 kHz stereo (`tools/bench_separate.py --seconds 240`). Model đã
nạp sẵn (server giữ model trong bộ nhớ; nạp lần đầu 16 s gồm tải trọng số, sau đó ~1 s).

| Cấu hình | Thời gian tách 4 phút |
|---|---|
| CPU, 32-bit, chồng đoạn 0,25 (mặc định demucs) | 85–97 s |
| GPU Apple (MPS), 32-bit, chồng 0,25 | 62–93 s |
| MPS, autocast 16-bit, chồng 0,25 | 38,5 s |
| **MPS, autocast 16-bit, chồng 0,1** (chọn) | **31,8–41,8 s**; 54–84 s khi máy thiếu RAM |

- Mỗi đoạn 7,8 s: 1,26–1,56 s (32-bit) / 1,14 s (autocast 16-bit). Ghép nhiều đoạn một lần không
  nhanh hơn (B = 2: 1,29 s / đoạn), B = 4 thì hết bộ nhớ GPU (8 GB dùng chung).
- 16-bit thuần không chạy được (STFT chưa có cho half); autocast giữ STFT 32-bit, lệch so với
  32-bit −57 dB (bfloat16 chậm hơn: 3,8 s / đoạn).
- Lúc đo, các ứng dụng khác chiếm swap 4,4 GB / 5 GB → các lần chậm (54–84 s) trùng lúc thiếu RAM.

Chất lượng (giọng vocadito 6, CC BY 4.0, lặp đủ 4 phút, trộn −3 dB vào beat — "SDR giọng" =
10·log10(Σ s² / Σ (s − ŝ)²)):

| Cấu hình | SDR giọng |
|---|---|
| MPS 32-bit, chồng 0,25 | 11,36 dB |
| MPS autocast 16-bit, chồng 0,25 | 11,46 dB |
| MPS autocast 16-bit, chồng 0,1 | 11,53 dB |

Beat không lời: stem giọng tách ra còn −53 dBFS RMS (gần như im).

Ước tổng thời gian bài 4 phút qua Wi-Fi: giải mã + FLAC 2–4 s, upload 4–10 s, tách 32–42 s, tải
giọng 3–8 s, phân tích lại ~1 s → **≈ 42–65 s** — sát chỉ tiêu; khi Mac thiếu RAM thì vượt.

## B3 — tổng thời gian qua server, upload gối với tách (2026-10-29)

`server/tools/client_sim.py` làm như app: `tcflac export` (giải mã + FLAC 44,1 kHz, đoạn 20 s) →
upload từng đoạn ngay khi có (giả lập tốc độ mạng) → hỏi trạng thái → tải giọng từng đoạn → xoá job.
Server chạy trên cùng Mac M1 (htdemucs, MPS, autocast 16-bit, chồng đoạn 0,1; cửa sổ 30 s chồng
2 s). Bài: beat 4 phút (12 đoạn, FLAC 26,6 MB).

| Mạng (upload) | Xuất FLAC xong | Upload xong | Có đoạn giọng đầu | **Tải giọng xong** | Server tách |
|---|---|---|---|---|---|
| không giới hạn | 1,3 s | 2,1 s | 7,5 s | **43,8 s** | 40,4 s |
| 20 Mbit/s (Wi-Fi) | 1,3 s | 12,1 s | 7,0 s | **42,4 s** | 38,3 s |
| 8 Mbit/s (4G) | 1,3 s | 28,1 s | 8,9 s | **45,4 s** | 40,0 s |

- Upload chạy gối với tách → mạng chậm chỉ thêm ~2 s. Phần tách (~40 s, gồm 7% chồng giữa các cửa
  sổ) là phần chính.
- Chưa gồm: thời gian iPhone giải mã + nén (B7 đo trên máy thật) và tải giọng qua Wi-Fi thật.

## B5 — app thật trên Mac (2026-10-29)

`app/test/separation_e2e_test.dart`: `FlacExporter` của engine (native) + `SeparationClient` (Dart)
với server htdemucs trên cùng Mac, bài 4 phút: **43,1 s** từ lúc bắt đầu tới khi tải xong đoạn giọng
cuối (server tách 39,6 s, nhận đủ bài sau 8,7 s); `loadBeatWithVocals` nạp lại được.

## B7 — trên iPhone (2026-10-31)

iPhone 13 Pro (`iPhone14,2`, iOS 26.6.2) qua Wi-Fi tới server htdemucs trên Mac M1 (cùng cấu hình
B1–B3). Bài gốc có giọng ca sĩ, tải từ kênh chính thức, chỉ để thử trong `testdata/vocal-songs/`
(không commit). Đường đi: Trang chủ → "Thêm bài hát" → "Tách" → đồng ý → tiến độ → phân tích lại
trên phần nhạc.

| Bài | Độ dài | **Tách xong** (iPhone) | Server tách | Quy về bài 4 phút |
|---|---|---|---|---|
| Duyên Phận – Lệ Quyên | 5:32 | **64 s** | 56 s | **≈ 46–48 s** ✅ (≤ 60 s) |
| Lạc Trôi – Sơn Tùng M-TP (MV) | 4:32 | tách được, chưa ghi số | — | — |

- Quy đổi: thời gian tách tỉ lệ với độ dài bài (56 s / 332 s → 40 s / 240 s), phần còn lại
  (iPhone giải mã + nén FLAC, upload, tải giọng về) ≈ 8 s và gần như không đổi theo bài → 46–48 s.
  Khớp số đo trên Mac (42–45 s).
- Lạc Trôi: dòng "Tách xong sau … s" chỉ hiện ngay lúc tách xong, mở lại bài thì mất → đã sửa
  (B7: lưu số đo vào `meta.json` của bản tách, mở lại bài vẫn thấy).

**Dò tone trước / sau khi tách** (trước = `keylab` trên bài gốc có giọng; sau = app trên iPhone,
phân tích phần nhạc):

| Bài | Đối chiếu | Trước khi tách | Sau khi tách |
|---|---|---|---|
| Lạc Trôi | beat karaoke trong bộ dev: Am, 67,5 BPM, không chuyển tone | Am, 67,5 BPM; timeline **sai**: B♭ (0:00–0:32) → Am → **G (0:50–1:40)** → Am | **Am, 68 BPM**; F (0:00–0:18, dạo đầu MV) → Am → Bm (4:20–hết, phần kết MV) |
| Duyên Phận | beat tone nam trong bộ dev: Cm (tone nữ thường cách một quãng 5 → Gm) | Gm, 84,9 BPM; B♭ (1:24–2:03, 3:22–4:01) | **Gm, 85 BPM**, không chuyển tone |

- Tách giọng bỏ được đoạn chuyển tone sai 50 s ở Lạc Trôi; tone + BPM sau tách khớp beat karaoke.
- Hai đoạn B♭ trưởng (tương đối) của Duyên Phận trước khi tách: chưa nghe kiểm là thật hay do giọng.
- Đoạn ngắn ở đầu / cuối bài (< 20 s: dạo đầu, phần kết MV) không còn hiện là "chuyển tone" trên
  màn Phân tích bài / Phòng thử (B7); timeline engine giữ nguyên.

**Nghe trên iPhone** (chủ dự án, 2026-10-31):

| Thử | Kết quả |
|---|---|
| "Giữ giọng ca sĩ gốc" 0% — còn lọt giọng ca sĩ? | **Không** |
| Kéo lên 100% — giống bài gốc? | **Có** |
| Đổi tone +2 — giọng và nhạc cùng lên, khớp nhịp? | **Vẫn khớp** |
