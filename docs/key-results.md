# Kết quả dò tone (M3)

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
