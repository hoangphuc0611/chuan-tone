# Kết quả dò tone, BPM, loudness (M3)

Chỉ tiêu M3 tính trên **bộ test 50 bài Việt** (CLAUDE.md mục 9) — chưa có dữ liệu, xem
`tools/keytest/README.md` để chuẩn bị. Các số dưới đây trên bộ công khai, chỉ để so sánh cấu hình và
chặn hồi quy.

Chạy lại: `tools/keytest/fetch_guitarset.sh && python3 tools/keytest/keytest.py run --csv testdata/GuitarSet/guitarset.csv`

## GuitarSet 1.1.0 (CC BY 4.0) — B3, 2026-10-23

360 đoạn guitar solo ~30 s (6 người chơi × 5 phong cách × 6 bài × đệm/solo), micro mono, nhãn tone
`key_mode` của bộ dữ liệu; 240 trưởng / 120 thứ, không chuyển tone. Khác xa beat karaoke Việt: chỉ
một cây guitar, nhiều jazz/funk (hợp âm 7, 9, giọng dorian/mixolydian).

**Cấu hình mặc định hiện tại** (temperley-kp, bass 0,25, chuẩn hoá L1 từng khung): **MIREX 0,706**,
kiểm chéo mir_eval khớp 360/360. Thời gian phân tích trung vị 0,19 s / đoạn 30 s (Mac M1, chạy song
song 8 luồng).

| Loại | Số bài | Tỉ lệ |
|---|---|---|
| Đúng | 225 | 62% |
| Lệch quãng 5 (lên) | 31 | 9% |
| Lệch quãng 5 (xuống) | 14 | 4% |
| Tương đối trưởng/thứ | 34 | 9% |
| Song song | 17 | 5% |
| Sai | 39 | 11% |

| Thể loại | Số bài | Điểm MIREX | Đúng hẳn |
|---|---|---|---|
| BN-comp | 36 | 0.869 | 81% |
| BN-solo | 36 | 0.761 | 69% |
| Funk-comp | 36 | 0.631 | 53% |
| Funk-solo | 36 | 0.500 | 39% |
| Jazz-comp | 36 | 0.619 | 56% |
| Jazz-solo | 36 | 0.578 | 50% |
| Rock-comp | 36 | 0.875 | 81% |
| Rock-solo | 36 | 0.575 | 42% |
| SS-comp | 36 | 0.933 | 92% |
| SS-solo | 36 | 0.717 | 64% |

Phần **đệm hợp âm** (gần với beat karaoke nhất) đạt 0,87–0,93 ở bossa nova, rock, singer-songwriter;
funk/jazz thấp vì hợp âm mở rộng và giọng dorian/mixolydian (lỗi hay gặp: C trưởng → Em do Cmaj7
chứa trọn Em; Am → G do giọng dorian).

### Quét cấu hình (4 profile × bass × chuẩn hoá khung)

| Profile | Bass | Chuẩn hoá | MIREX |
|---|---|---|---|
| temperley-kp | 0.25 | l1 | 0.706 |
| temperley-kp | 0 | l1 | 0.697 |
| temperley-kp | 0.25 | none | 0.679 |
| temperley-kp | 0 | none | 0.675 |
| temperley | 0 | l1 | 0.674 |
| temperley-kp | 0.5 | l1 | 0.671 |
| temperley | 0.25 | l1 | 0.659 |
| temperley | 0 | none | 0.642 |
| temperley | 0.25 | none | 0.637 |
| diatonic | 0.25 | none | 0.634 |
| temperley-kp | 0.5 | none | 0.633 |
| temperley | 0.5 | l1 | 0.626 |
| diatonic | 0 | none | 0.625 |
| kk | 0.25 | l1 | 0.623 |
| temperley | 0.5 | none | 0.619 |
| diatonic | 0.25 | l1 | 0.619 |
| kk | 0.5 | l1 | 0.618 |
| diatonic | 0 | l1 | 0.613 |
| kk | 0.25 | none | 0.608 |
| kk | 0 | l1 | 0.607 |
| kk | 0 | none | 0.600 |
| kk | 0.5 | none | 0.581 |
| diatonic | 0.5 | none | 0.580 |
| diatonic | 0.5 | l1 | 0.562 |

Nhận xét: chuẩn hoá L1 từng khung luôn tốt hơn; bass 0,5 làm giảm điểm (guitar solo gần như không có
bass thật — beat karaoke có bass riêng nên phải chọn lại trên tập dev Việt ở B4).
Profile "temperley" (1999) chưa đối chiếu được số liệu gốc; "kk" và "temperley-kp" đã đối chiếu.

## Timeline chuyển tone (B5, 2026-10-23)

HMM 24 trạng thái trên cửa sổ 6 s / bước 2 s (`engine/src/key/key_timeline.*`). Test tổng hợp
(`KeyTimeline.*`, `SongAnalyzer.PipelineTracksModulation`): Am → A (kiểu bolero), nâng nửa cung cuối
bài, Am → C → Am đều tìm đúng tone, biên lệch ≤ 4 s (≤ 6 s với chuyển tương đối); đoạn mượn 6 s và
khoảng lặng 10 s không sinh chuyển tone.

GuitarSet **không có bài nào chuyển tone**, nên chỉ đo được mặt trái: số bài bị đoán chuyển tone giả
và điểm timeline tính trên mọi bài (thứ engine dùng khi chỉnh giọng theo timeline). Đoạn ~30 s nên
`min-segment` 20 s gần như cấm hẳn đoạn thứ hai — không dùng để chọn tham số cho bài 4 phút.

| ε (switch-prob) | τ (emission-temp) | min-segment | Timeline mọi bài | Chuyển tone giả |
|---|---|---|---|---|
| 0,01 | 0,05 | 12 s | 0,692 | 43/360 |
| 0,01 | 0,1 | 12 s | 0,702 | 23/360 |
| **0,002** | **0,1** | **12 s** | **0,702** | **19/360** (mặc định) |
| 0,0005 | 0,1 | 12 s | 0,703 | 14/360 |
| 0,002 | 0,1 | 20 s | 0,707 | 2/360 |

Tone toàn bài vẫn 0,706. Trên 19 bài bị chuyển giả, điểm timeline 0,383 so với 0,453 nếu giữ một
tone — các đoạn đó (funk/jazz) cục bộ thật sự nghiêng về tone khác: đã thử bắt đoạn khác tone phải
thắng tone toàn bài một khoảng tối thiểu (0,03 / 0,06 điểm tương quan) — không giảm được bài nào nên
bỏ. Tham số timeline chốt trên tập dev Việt có chuyển tone (B4).

## BPM (B6, 2026-10-23)

Onset (spectral flux trên log biên độ của phần bộ gõ sau HPSS) → tự tương quan + răng lược bội chu
kỳ → prior log-Gauss 120 BPM, σ 1,4 quãng 8 (`engine/src/key/tempo.*`). Test tổng hợp: tiếng gõ
90–140 BPM sai ≤ 1%; 60–180 BPM đúng trong một quãng 8 (×2 / ×½).

GuitarSet (tempo theo lead sheet; guitar không có trống): **Acc1 58% / Acc2 83%**. Nhầm quãng 8 chia
đều hai phía (×2: 50 bài, ×½: 36 bài) → tâm prior không lệch hẳn.

| Prior | σ (quãng 8) | Acc1 | Acc2 |
|---|---|---|---|
| 100 | 0,8 | 58% | 76% |
| 100 | 1,4 | 56% | 81% |
| 120 | 0,8 | 70% | 80% |
| **120** | **1,4** | **58%** | **83%** (mặc định) |
| 140 | 0,8 | 61% | 84% |
| 140 | 1,4 | 57% | 85% |

Giữ mặc định của Ellis 2007: prior hẹp quanh 120 sẽ đẩy bolero (nhạc công đếm ~65) lên ~130. Prior
theo thể loại chọn trên tập dev Việt (B4); chỉ tiêu đề xuất Acc2 ≥ 90%, Acc1 ≥ 75%.

## Loudness (B6, 2026-10-23)

ITU-R BS.1770-4 (`engine/src/key/loudness.*`): hệ số K-weighting tính lại được đúng bảng 48 kHz của
chuẩn (sai < 1e-8); 5 ca test EBU Tech 3341 (sin 1 kHz stereo, có cổng tuyệt đối/tương đối) đều
trong ±0,1 LU ở cả 48 kHz và 44,1 kHz. Beat mono tính như mono kép, file > 2 kênh chỉ đo 2 kênh đầu
(đúng tín hiệu engine phát). Mức đích chuẩn hoá beat chốt ở M5.

## Thời gian phân tích

Bài tổng hợp 4 phút stereo 48 kHz, Mac M1, Release: **1,12 s** (giải mã 0,03 · LUFS 0,12 · hạ mẫu
0,12 · HPSS 0,52 · CQT 0,15 · tone 0,001 · BPM 0,19). Ngân sách: ≤ 5 s trên iPhone 13 Pro (đo ở B9).
