# Kết quả đổi tone beat (M4)

Chỉ tiêu M4 (CLAUDE.md mục 9): **không méo, không nhòe trống khi nghe A/B** — nghiệm thu bằng tai
(trang nghe mù `tools/stretch/ab.py` + iPhone, xem `docs/m4-plan.md` điểm 3). Các số khách quan
dưới đây đo trên bộ tín hiệu tổng hợp có đáp án (`engine/tests/util/stretch_eval.*`), dùng để chọn
thuật toán / tham số và chặn hồi quy trong CI.

## Chỉ số

| Chỉ số | Tín hiệu | Cách đo |
|---|---|---|
| Cao độ | sin 440 Hz; La3 8 hoạ âm | đỉnh phổ (Blackman–Harris, nội suy parabol) so với f·2^(n/12) |
| Ngoài hoạ âm | La3; hợp âm Đô trưởng + bass | năng lượng ngoài ±max(8 bin, 10 cent) quanh các hoạ âm đã dịch / tổng — tiếng "phơ", điều biên, sai tần số |
| Trước onset | kick / snare / hi-hat đứng riêng (80 BPM) | năng lượng 5–50 ms trước tiếng đánh / đỉnh — **nhòe trước** (pre-echo) |
| Thời gian lên | như trên | 10→90% bao biên 1 ms, bản ra trừ bản gốc — độ sắc tiếng đánh |
| Dội sau đỉnh | như trên | bao biên 5 ms tăng lại sau đỉnh (từ +5 ms) / đỉnh — **trống đánh đôi** |
| Lệch onset | như trên | lệch thời gian làm bao biên khớp bản gốc nhất (±20 ms) |
| Δ tương quan L–R | beat stereo (hợp âm trái, hi-hat phải, trống giữa) | ảnh stereo |
| Δ LUFS | như trên | BS.1770 (`key/loudness`) |

## B1 — mốc dưới và mốc trên (2026-10-26)

`beatlab eval` (Mac M1, Release). **pv** = `PitchShifter` B1: phase vocoder cơ bản (Flanagan &
Golden; Dolson), FFT 4096 / hop 1024, chưa khoá pha, chưa giữ transient, từng kênh riêng.
**signalsmith** = Signalsmith Stretch 1.4.0 `presetDefault` (hộp đen, chỉ để so sánh).

| Thuật toán, mức | Cao độ sin (cent) | Cao độ hoạ âm (cent) | Ngoài hoạ âm: nốt (dB) | Ngoài hoạ âm: hợp âm (dB) | Trước onset: trung vị / tệ nhất (dB) | Thời gian lên +ms | Dội sau đỉnh (dB) | Lệch onset (ms) | Δ tương quan L–R | Δ LUFS | × thời gian thực |
|---|---|---|---|---|---|---|---|---|---|---|---|
| gốc (chưa đổi) | 0.00 | 0.00 | -60.0 | -65.1 | -161.4 / -49.8 | 1.31 | -18.8 | 0.00 | 0.908 | -21.07 | — |
| pv -6 | 0.00 | 0.00 | -30.2 | -29.0 | -21.6 / -6.1 | 7.48 | -11.6 | 16.67 | 0.872 | -3.83 | 77 |
| pv -4 | 0.00 | 0.00 | -37.3 | -36.4 | -25.5 / -9.2 | 8.82 | -14.3 | 10.29 | 0.612 | -3.95 | 72 |
| pv -2 | 0.00 | 0.00 | -47.5 | -41.9 | -25.4 / -5.7 | 6.65 | -9.2 | 6.25 | 0.585 | -3.27 | 67 |
| pv +2 | 0.00 | 0.00 | -52.7 | -46.6 | -22.9 / -7.3 | 6.36 | -1.5 | 8.52 | 0.761 | -3.29 | 55 |
| pv +4 | 0.00 | 0.00 | -48.2 | -41.9 | -10.2 / -6.7 | 2.10 | -14.3 | 17.48 | 0.893 | -2.47 | 48 |
| pv +6 | 0.00 | 0.00 | -44.8 | -41.7 | -9.5 / -7.4 | 8.03 | -10.8 | 11.79 | 0.885 | -3.03 | 39 |
| signalsmith -6 | 5.56 | 13.22 | -10.6 | -14.6 | -33.0 / -28.1 | 3.31 | -11.4 | 1.21 | 0.001 | -0.80 | 134 |
| signalsmith -4 | 2.64 | 1.57 | -50.6 | -23.9 | -35.5 / -32.3 | 2.54 | -13.0 | 0.77 | 0.000 | -0.53 | 133 |
| signalsmith -2 | 3.50 | 3.90 | -34.6 | -34.7 | -40.2 / -34.1 | 0.75 | -15.7 | 4.96 | 0.000 | -0.28 | 132 |
| signalsmith +2 | 2.08 | 5.67 | -19.4 | -37.2 | -39.1 / -35.9 | 1.11 | -18.1 | 0.67 | 0.002 | -0.09 | 130 |
| signalsmith +4 | 4.24 | 1.37 | -50.1 | -18.9 | -37.1 / -35.0 | 2.12 | -17.0 | 1.29 | 0.002 | -0.09 | 130 |
| signalsmith +6 | 1.41 | 6.73 | -20.9 | -36.1 | -34.5 / -32.9 | 2.25 | -17.1 | 0.60 | 0.001 | -0.13 | 130 |

Đọc bảng:
- **pv cơ bản**: cao độ đúng tuyệt đối (0,00 cent) nhưng đúng các lỗi kinh điển của phase vocoder
  chưa khoá pha — trống nhòe (năng lượng trước onset −10…−26 dB, tiếng đánh chậm thêm 2–9 ms, lệch
  6–17 ms), mất ảnh stereo (tương quan L–R lệch 0,6–0,9), nhỏ đi 2,5–4 dB (các khung chồng nhau lệch
  pha triệt tiêu một phần).
- **Lệch thời gian của pv cơ bản** (test `DISABLED_TimeAlignedWithOriginal`): tâm năng lượng của một
  cục sin lệch ≈ (1 − β)·35 ms (+6: −14,6 ms; −6: +10,2 ms), đã đối chiếu bằng bản numpy độc lập ra
  đúng số. Nguyên nhân: độ nghiêng pha giữa các bin (vị trí của âm trong khung) được cộng theo tần
  số tức thời × Hs nên mọi thay đổi của nó bị nhân β thay vì bám theo phổ phân tích → lệch tích luỹ.
  Khoá pha identity (B2) lấy lại quan hệ pha giữa các bin từ phổ phân tích mỗi khung.
- **Signalsmith**: trống sắc (trước onset −33…−40 dB, lên +0,8…3,3 ms), stereo và âm lượng giữ tốt,
  nhưng **lệch cao độ ổn định 1,4–13 cent** (đo trên từng đoạn 0,25 s đều cùng số, không phải dao
  động). Với karaoke, beat lệch như vậy sẽ chênh với giọng đã chỉnh về lưới của beat — nên chỉ tiêu
  cao độ ≤ 1 cent giữ nguyên dù mốc trên không đạt. Cột "ngoài hoạ âm" của Signalsmith cao chủ yếu vì
  lệch cao độ (hoạ âm cao rơi ra ngoài ±10 cent).
- **Tốc độ**: beat thật 4 phút (bac-phan-remix, 249 s stereo 44,1 kHz) ở +4: pv **5,0 s** (50× thời
  gian thực), Signalsmith 2,3 s (110×). Ngân sách ≤ 4 s trên iPhone 13 Pro → B2 cần nhanh hơn 2–3
  lần: dùng chung trọng số nội suy cho hai kênh, chung pha stereo (bớt atan2 / sin / cos), FFT nhanh
  hơn.

## Chỉ tiêu đề xuất (chốt sau B3)

| Chỉ số | Chỉ tiêu | pv B1 | Signalsmith |
|---|---|---|---|
| Cao độ | ≤ 1 cent | đạt | không đạt (1,4–13) |
| Trước onset (trung vị / tệ nhất) | ≤ −30 / −25 dB | không đạt | đạt (−33…−40 / −28…−36) |
| Thời gian lên | ≤ +2 ms ở ±1…4, ≤ +3 ms ở ±5…6 | không đạt | gần đạt (−6: 3,3) |
| Dội sau đỉnh | ≤ −12 dB (gốc −18,8) | không đạt | gần đạt (−11,4…−18,1) |
| Lệch onset | ≤ 1 ms | không đạt | không đạt ở −2 (5,0) |
| Ngoài hoạ âm (nốt / hợp âm) | ≤ −40 / −35 dB | không đạt ở ±5…6 | — (lệch cao độ) |
| Δ tương quan L–R | ≤ 0,02 | không đạt | đạt |
| Δ LUFS | ≤ 0,5 LU | không đạt | gần đạt (−6: −0,8) |
| Tốc độ (M1) | ≥ 100× thời gian thực | 50× | 110× |
