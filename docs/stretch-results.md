# Kết quả đổi tone beat (M4)

Chỉ tiêu M4 (CLAUDE.md mục 9): **không méo, không nhòe trống khi nghe A/B** — nghiệm thu bằng tai
(trang nghe mù `tools/stretch/ab.py` + iPhone, xem `docs/m4-plan.md` điểm 3). Các số khách quan
dưới đây đo trên bộ tín hiệu tổng hợp có đáp án (`engine/tests/util/stretch_eval.*`), dùng để chọn
thuật toán / tham số và chặn hồi quy (`PitchShifter.*` trong ctest, `BM_PitchShifter` trong bench).

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
| Δ LUFS | như trên | BS.1770 (`key/loudness`) — chỉ để tham khảo, xem B2 |
| Nhiễu so với lý tưởng | nhiễu trắng | công suất bản ra / lý tưởng (giữ nguyên khi hạ tone; × 0,97/β khi nâng tone vì phải bỏ phần trên Nyquist/β) |

## Bảng hiện tại (B2, 2026-10-26)

`beatlab eval --algo pv,pv-basic,signalsmith` (Mac M1, Release, 48 kHz).
- **pv** = `PitchShifter` mặc định: khoá pha identity + chung pha stereo, FFT 4096 / hop 1024.
- **pv-basic** = phase vocoder cơ bản của B1 (mốc dưới, `phaseLock = false`).
- **signalsmith** = Signalsmith Stretch 1.4.0 `presetDefault` (hộp đen, mốc trên).

| Thuật toán, mức | Cao độ sin (cent) | Cao độ hoạ âm (cent) | Ngoài hoạ âm: nốt (dB) | Ngoài hoạ âm: hợp âm (dB) | Trước onset: trung vị / tệ nhất (dB) | Thời gian lên +ms | Dội sau đỉnh (dB) | Lệch onset (ms) | Δ tương quan L–R | Δ LUFS | Nhiễu so với lý tưởng (dB) | × thời gian thực |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gốc (chưa đổi) | 0.00 | 0.00 | -60.0 | -65.1 | -161.3 / -53.0 | 1.31 | -18.8 | 0.00 | 0.902 | -20.92 | 0.00 | — |
| pv -6 | 0.00 | 0.00 | -59.7 | -37.6 | -21.0 / -18.2 | 17.42 | -9.7 | 5.46 | 0.005 | -1.45 | -1.40 | 171 |
| pv -4 | 0.00 | 0.00 | -59.4 | -39.6 | -24.7 / -21.3 | 17.45 | -11.9 | 4.27 | 0.002 | -0.90 | -1.08 | 158 |
| pv -2 | 0.00 | 0.00 | -59.6 | -44.7 | -29.2 / -26.4 | 10.34 | -15.4 | 5.35 | 0.000 | -0.42 | -0.76 | 143 |
| pv +2 | 0.00 | 0.00 | -59.4 | -45.1 | -29.1 / -26.3 | 7.57 | -16.0 | 6.08 | 0.002 | -0.18 | -0.58 | 118 |
| pv +4 | 0.00 | 0.00 | -67.4 | -41.1 | -25.9 / -21.6 | 9.86 | -15.6 | 2.35 | 0.002 | -0.29 | -0.67 | 103 |
| pv +6 | 0.00 | 0.00 | -77.7 | -41.4 | -24.9 / -22.3 | 10.06 | -16.0 | 2.73 | 0.002 | -0.38 | -0.75 | 97 |
| pv-basic -6 | 0.00 | 0.00 | -30.2 | -29.0 | -21.7 / -6.2 | 6.95 | -11.6 | 16.67 | 0.819 | -3.84 | -3.46 | 92 |
| pv-basic -4 | 0.00 | 0.00 | -37.3 | -36.4 | -25.6 / -9.1 | 8.82 | -14.3 | 10.29 | 0.671 | -3.93 | -3.43 | 84 |
| pv-basic -2 | 0.00 | 0.00 | -47.5 | -41.9 | -25.1 / -5.9 | 6.62 | -9.3 | 6.40 | 0.530 | -3.34 | -3.28 | 79 |
| pv-basic +2 | 0.00 | 0.00 | -52.7 | -46.6 | -22.7 / -7.2 | 8.12 | -1.7 | 8.46 | 0.704 | -3.28 | -3.14 | 63 |
| pv-basic +4 | 0.00 | 0.00 | -48.2 | -41.9 | -10.3 / -6.8 | 2.11 | -14.3 | 17.48 | 0.830 | -2.86 | -3.12 | 55 |
| pv-basic +6 | 0.00 | 0.00 | -44.8 | -41.7 | -9.4 / -7.4 | 8.51 | -10.8 | 11.79 | 0.940 | -2.83 | -3.04 | 49 |
| signalsmith -6 | 5.56 | 13.22 | -10.6 | -14.6 | -32.8 / -28.1 | 3.33 | -11.4 | 1.21 | 0.001 | -0.78 | -1.02 | 128 |
| signalsmith -4 | 2.64 | 1.57 | -50.6 | -23.9 | -35.6 / -32.3 | 2.54 | -13.0 | 0.77 | 0.000 | -0.51 | -0.80 | 129 |
| signalsmith -2 | 3.50 | 3.90 | -34.6 | -34.7 | -40.4 / -34.1 | 0.76 | -15.7 | 4.96 | 0.000 | -0.27 | -0.47 | 129 |
| signalsmith +2 | 2.08 | 5.67 | -19.4 | -37.2 | -39.7 / -35.9 | 0.95 | -18.1 | 0.67 | 0.002 | -0.11 | -0.37 | 123 |
| signalsmith +4 | 4.24 | 1.37 | -50.1 | -18.9 | -37.7 / -35.0 | 2.12 | -17.0 | 1.29 | 0.003 | -0.13 | -0.59 | 123 |
| signalsmith +6 | 1.41 | 6.73 | -20.9 | -36.1 | -35.3 / -32.9 | 2.11 | -17.1 | 0.60 | 0.003 | -0.18 | -0.66 | 127 |

## B2 — khoá pha identity + chung pha stereo

- **Căn thời gian và âm lượng phần có cao độ: đúng tuyệt đối.** Cục sin (bao Gauss σ 50 / 200 ms,
  330 / 1000 Hz) giữ tâm năng lượng ±0,00 ms và mức ±0,02 dB ở mọi mức −6…+6 (B1: lệch tới 14,6 ms,
  −1,5 dB). Sin ổn định 440 / 1000 Hz: 0,00 dB ở cả hop 1024 và 512.
- **Tạp phổ**: một nốt −59…−78 dB (B1 −30…−53), hợp âm −38…−45 dB (B1 −29…−47).
- **Ảnh stereo**: tương quan L–R lệch ≤ 0,005 (B1 0,6–0,9) — cùng góc xoay cho L và R từng bin.
- **Nhiễu** hụt −0,6…−1,4 dB so với lý tưởng (B1 −3,0…−3,5, đúng mức lý thuyết −3,1 dB khi các khung
  chồng nhau hoàn toàn không đồng pha; Signalsmith −0,4…−1,0). Các khung nhiễu chồng nhau lấy từ
  những đoạn gốc lệch nhau |Ha − Hs| mẫu nên cộng không đồng pha — vốn có của phase vocoder.
- **LUFS không phải chỉ số đúng** để đòi "giữ nguyên": hạ tone dồn năng lượng khỏi vùng 2–8 kHz
  (K-weighting cộng +4 dB) nên LUFS giảm dù công suất giữ nguyên. Trên 3 beat dev thật (remix, bolero,
  nhạc trẻ), pv và Signalsmith đi cùng nhau: −6: −1,34 / −1,87 / −1,66 (Signalsmith −1,11 / −1,46 /
  −1,53); −3: −0,66 / −0,91 / −0,83 (−0,65 / −0,77 / −0,88); +3 và +6 trong ±0,4 (±0,5). Chênh với
  Signalsmith ≤ 0,41 LU.
- **Trống**: khoá pha làm tiếng đánh **chậm hơn** B1 (thời gian lên +7,6…17 ms so với +2…9 ms): cả
  vùng phổ xoay cùng góc nên năng lượng transient bị trải theo khung. Việc của B3.
- **Hop 512** (chồng 8): ngoài hoạ âm hợp âm tốt hơn ~2 dB, trước onset tốt hơn 1–2 dB nhưng chậm gấp
  đôi → **giữ hop 1024**.
- **Tốc độ** (`BM_PitchShifter`, 10 s stereo 48 kHz, M1): −6 176×, +4 101×, +6 96× thời gian thực
  (B1 39–77×). Beat thật 4 phút 44,1 kHz ở +4 / +6: **2,37 s** (B1 5,0 s). Nhờ: chỉ tính atan2 /
  sincos cho đỉnh phổ (thay vì mọi bin × mọi kênh), trọng số nội suy tính một lần cho mọi kênh, bảng
  sinc đa pha + vòng đệm có ô "gương" (đọc liên tục, tự vector hoá). FFT còn ~1,2 s / bài; nếu đo trên
  iPhone vượt 4 s → FFT nhanh hơn (vDSP trên iOS, PFFFT BSD trên Android).

## B1 — mốc dưới và mốc trên

- **pv-basic**: cao độ đúng tuyệt đối nhưng đúng các lỗi kinh điển của phase vocoder chưa khoá pha —
  trống nhòe, mất ảnh stereo, nhỏ đi ~3 dB với nhiễu, lệch thời gian ≈ (1 − β)·35 ms (+6: −14,6 ms;
  −6: +10,2 ms; đối chiếu bằng bản numpy độc lập ra đúng số). Nguyên nhân: độ nghiêng pha giữa các bin
  (vị trí của âm trong khung) được cộng theo tần số tức thời × Hs nên mọi thay đổi của nó bị nhân β
  thay vì bám theo phổ phân tích. Lỗi đã sửa ở B1: bắt đầu từ khung im lặng (pha "rác") làm sai lệch
  vĩnh viễn → đặt lại pha cả khung khi khung trước im lặng.
- **Signalsmith**: trống sắc (trước onset −33…−40 dB, lên +0,8…3,3 ms), stereo giữ tốt, nhưng **lệch
  cao độ ổn định 1,4–13 cent** (từng đoạn 0,25 s đều cùng số). Với karaoke, beat lệch như vậy sẽ chênh
  với giọng đã chỉnh về lưới của beat → chỉ tiêu cao độ ≤ 1 cent giữ nguyên dù mốc trên không đạt.
  Cột "ngoài hoạ âm" của Signalsmith cao chủ yếu vì lệch cao độ.

## Chỉ tiêu (cập nhật B2, chốt sau B3)

| Chỉ số | Chỉ tiêu | pv (B2) | Signalsmith |
|---|---|---|---|
| Cao độ | ≤ 1 cent | ✅ 0,00 | ❌ 1,4–13 |
| Căn thời gian (cục sin) | ≤ 0,5 ms | ✅ 0,00 | — |
| Âm lượng phần có cao độ | ±0,1 dB | ✅ ±0,02 | — |
| Ngoài hoạ âm (nốt / hợp âm) | ≤ −40 / −35 dB | ✅ −59 / −38 | — (lệch cao độ) |
| Δ tương quan L–R | ≤ 0,02 | ✅ ≤ 0,005 | ✅ |
| Nhiễu so với lý tưởng | ≥ −1,5 dB | ✅ −0,6…−1,4 | ✅ −0,4…−1,0 |
| LUFS beat thật so với Signalsmith | ≤ 0,5 LU | ✅ ≤ 0,41 | — |
| Trước onset (trung vị / tệ nhất) | ≤ −30 / −25 dB | ❌ (B3) | ✅ |
| Thời gian lên | ≤ +2 ms ở ±1…4, ≤ +3 ms ở ±5…6 | ❌ (B3) | gần đạt |
| Dội sau đỉnh | ≤ −12 dB (gốc −18,8) | gần đạt | gần đạt |
| Lệch onset | ≤ 1 ms | ❌ (B3) | ❌ ở −2 |
| Tốc độ (M1, 48 kHz) | ≥ 100× (ngân sách iPhone ≤ 4 s / bài 4 phút) | +6: 96× | 130× |

Bỏ chỉ tiêu "Δ LUFS ≤ 0,5 trên beat tổng hợp" của B1 (xem phần LUFS ở trên).
