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
