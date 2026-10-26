# Kết quả dò tone, BPM, loudness (M3)

Chỉ tiêu M3 tính trên **bộ test 50 bài Việt** (CLAUDE.md mục 9): **0,864 ≥ 0,85 — đạt** (chấm một
lần, 2026-10-26, commit b215032). Các số khác trên nhạc Việt là tập dev.

## Nhạc Việt — bộ test (B9, 2026-10-26)

50 beat karaoke (bolero 22, ballad 13, nhạc trẻ 9, dân ca 4, remix 2; 40 thứ / 10 trưởng), nhãn =
tone ghi trong tiêu đề, cùng cách lấy với dev nhưng khác bài (đã đối chiếu tên bài và link: không
trùng; "Hồng Nhan" ở dev và "Ải Hồng Nhan" remix ở test là hai bài khác nhau). Cấu hình mặc định của
engine, không tham số thêm. Lần chạy bộ test **thứ 1** (`testdata/vn-key/test-runs.jsonl`).

| Chỉ số | Giá trị |
|---|---|
| **Điểm MIREX** (chỉ tiêu ≥ 0,85) | **0,864** |
| Tính lệch quãng 5 cả hai chiều | 0,884 |
| Đúng hẳn | 43/50 (86%) |
| Lệch quãng 5 xuống (tính 0) / song song / sai | 2 / 1 / 4 |
| Điểm timeline so với nhãn một tone | 0,818 |
| Kiểm chéo mir_eval | khớp 50/50 |

| Thể loại | Số bài | MIREX | Đúng hẳn |
|---|---|---|---|
| bolero | 22 | 0,909 | 91% |
| ballad | 13 | 0,923 | 92% |
| nhạc trẻ | 9 | **0,578** | 56% |
| dân ca | 4 | 1,000 | 100% |
| remix | 2 | 1,000 | 100% |

Bài không đúng hẳn:

| Bài | Nhãn | Máy | Ghi chú |
|---|---|---|---|
| Hơn Cả Yêu | F | F# | tuning −49 cent: beat nằm giữa F và F#; F#(−49) cùng lưới cao độ với F(+51) → chỉnh giọng vẫn khớp beat |
| Nửa Đêm Ngoài Phố | Gm | Abm | tuning ~0, độ tin cậy 1,0 — nghi audio khác tone ghi ở tiêu đề |
| Thư Tình Em Gái | Em | Dm | tuning ~0, độ tin cậy 1,0 — nghi như trên |
| Thà Rằng Như Thế | Bbm | Am | tuning ~0, độ tin cậy 0,89 — nghi như trên |
| Tình Nhạt Phai | Dm | Gm | lệch quãng 5 xuống (MIREX tính 0) |
| Trái Tim Bên Lề | Dm | Gm | lệch quãng 5 xuống |
| Tình Thôi Xót Xa | Ebm | Eb | song song (0,2) |

Đọc kết quả:
- Vượt chỉ tiêu 0,014 điểm — **chưa tới 1 bài** (mỗi bài 0,02). Sai số chuẩn trên 50 bài ≈ 0,045,
  nên 0,864 khớp với ước lượng kiểm chéo trên dev (0,790); test cao hơn chủ yếu vì có nhiều bolero
  và ballad (hai thể loại máy làm tốt), ít nhạc trẻ.
- **Nhạc trẻ là điểm yếu** ở cả dev (0,77) lẫn test (0,58): lỗi quãng 5 xuống (nhầm sang tone hạ át)
  và song song. Cải tiến tiếp nên nhắm vào thể loại này (cần thêm bài nhạc trẻ ở dev).
- Điểm chính thức giữ theo nhãn tiêu đề; không sửa nhãn sau khi xem kết quả (sửa chỉ những bài máy
  sai sẽ làm điểm lạc quan).

## Nhạc Việt — tập dev (B4, 2026-10-26)

50 beat karaoke (bolero 27, nhạc trẻ 11, ballad 6, remix 4, dân ca 2; 36 thứ / 14 trưởng), nhãn =
tone kênh karaoke ghi trong tiêu đề (vd. "Tone Nam ( Cm )") — độc lập với ToneCore, chỉ có tone
chính. Chủ dự án chọn dùng nhãn tiêu đề, không nghe kiểm (2026-10-26). Nguồn và cách lấy nhãn:
`tools/keytest/README.md`, `docs/licenses.md`. Mỗi bài ≈ 0,02 điểm.

| Cấu hình | MIREX dev | Ghi chú |
|---|---|---|
| B3 (temperley-kp, bass 0,25, L1) | 0,734 | |
| + gợi ý chủ âm cuối bài 0,05 | 0,758 | cũng tăng trên GuitarSet (0,706 → 0,712) |
| + prior giọng thứ 0,03 hoặc phân xử tương đối bằng bass 0,05 | 0,772 | < 1 bài, GuitarSet giảm → không bật |
| bằng chứng nốt cảm (hợp âm át trưởng) | không đổi | bài thứ Việt dùng ♭7, gần như không có nốt cảm → đã gỡ |
| **profile "vn-karaoke" học từ dev — kiểm chéo 5 phần** | **0,790** | tốt hơn ở 3/5 phần, không phần nào kém |
| profile "vn-karaoke" (mặc định mới) — chấm trên chính dev | 0,806 | lạc quan (đã học từ dev); dùng số kiểm chéo |

Mặc định từ 2026-10-26: **vn-karaoke**, bass 0,25, L1, gợi ý cuối bài 0,05. Theo thể loại (chấm trên
dev): bolero 0,80 · nhạc trẻ 0,77 · ballad 0,72 · remix 1,00 · dân ca 1,00. Lỗi còn: 2 tương đối
trưởng/thứ, 2 lệch quãng 5, 6 "sai" (trong đó có bài nghi nhãn tiêu đề sai, vd. Hoa Trinh Nữ G / máy
F#, tuning ~0 cent).

Đổi lại, "vn-karaoke" kém hơn trên nhạc không phải Việt: GuitarSet 0,642 (temperley-kp 0,712) → job
CI `key-eval` ghim temperley-kp để vẫn chặn hồi quy cả chuỗi trên dữ liệu công khai.

Profile học được: thứ — bậc 1 (0,16) và 5 (0,17) nổi nhất, ♭7 (0,096) mạnh hơn nốt cảm 7 (0,048)
(ngược với temperley-kp: 7 > ♭7) → nhạc Việt giọng thứ dùng thứ tự nhiên / ngũ cung nhiều.

Timeline: 22/50 bài dev (19/50 bài test) được máy báo có chuyển tone. Chủ dự án xác nhận
(2026-10-26) các bài này **thật sự đổi tone liên tục trong bài** (kiểu phiên khúc thứ ↔ điệp khúc
trưởng tương đối / song song) → giữ nguyên tham số timeline. Nhãn tiêu đề chỉ có tone chính, nên số
"chuyển tone giả" và điểm timeline trong báo cáo `keytest` ở các bài này không phản ánh lỗi (điểm
timeline là cận dưới).

**Mốc kiểm B4 (dev < 0,80)**: ước lượng kiểm chéo 0,790 — sát mốc. Bộ test: 0,864 (xem trên).

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

## Công cụ cho B4, thử trước trên GuitarSet (2026-10-23)

- **Profile học từ dữ liệu** (`tools/keytest/train_profile.py`, kiểm chéo 5 phần): **0,688** so với
  profile hiện tại 0,706 trên cùng các phần (−0,018) — trung bình chroma đơn giản chưa thắng
  temperley-kp trên guitar jazz/funk. GuitarSet chỉ có 30 bản nhạc (mỗi bản 6 người chơi) nên các
  phần kiểm chéo không tách bài hoàn toàn — số này chỉ tham khảo; phải đo lại trên dev nhạc Việt.
- **Gợi ý chủ âm cuối bài** (`--end-weight`, cửa sổ 4 s):

| end-weight | MIREX | Timeline mọi bài |
|---|---|---|
| 0 (mặc định) | 0,706 | 0,702 |
| 0,05 | **0,718** | 0,711 |
| 0,1 | 0,704 | 0,694 |
| 0,2 | 0,666 | 0,657 |

  Lợi ít và giảm nhanh khi tăng trọng số (beat lặp vòng có thể kết ở bậc V) → giữ tắt, chọn lại
  trên dev.

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

**Chưa đo được trên nhạc Việt**: nhãn tiêu đề không có BPM, nên dev và test chưa có nhãn BPM. Chỉ tiêu
BPM còn mở cho tới khi có nhãn BPM độc lập (vd. người gõ nhịp 20–30 bài).

## Loudness (B6, 2026-10-23)

ITU-R BS.1770-4 (`engine/src/key/loudness.*`): hệ số K-weighting tính lại được đúng bảng 48 kHz của
chuẩn (sai < 1e-8); 5 ca test EBU Tech 3341 (sin 1 kHz stereo, có cổng tuyệt đối/tương đối) đều
trong ±0,1 LU ở cả 48 kHz và 44,1 kHz. Beat mono tính như mono kép, file > 2 kênh chỉ đo 2 kênh đầu
(đúng tín hiệu engine phát). Mức đích chuẩn hoá beat chốt ở M5.

## Thời gian phân tích

Bài tổng hợp 4 phút stereo 48 kHz, Mac M1, Release: **1,12 s** (giải mã 0,03 · LUFS 0,12 · hạ mẫu
0,12 · HPSS 0,52 · CQT 0,15 · tone 0,001 · BPM 0,19). Ngân sách: ≤ 5 s trên iPhone 13 Pro — **chưa đo
trên máy thật**. Bộ test (m4a 3–8 phút, chạy song song nhiều bài trên Mac M1): trung vị 2,49 s, lớn
nhất 3,74 s.
