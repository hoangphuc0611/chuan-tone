# Kết quả đổi tone beat (M4)

> Tình trạng 2026-10-27: đã nghe A/B mù 2 lần (xem "B4") — chủ dự án chốt chất lượng hiện tại; còn
> nghe và đo trên iPhone (B7).

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
| Dội sau đỉnh | như trên | bao biên 5 ms **của sai phân bậc 1** (nhấn cao tần) tăng lại sau đỉnh (từ +5 ms) / đỉnh — **trống đánh đôi** (từ B3; trước đó đo trên tín hiệu gốc, bị thân kick trầm gợn theo nửa chu kỳ làm nhiễu) |
| Lệch onset | như trên | lệch thời điểm bao biên 1 ms vượt 10% đỉnh so với bản gốc (từ B3; trước đó dùng tương quan bao biên — bị đuôi trống ngắn / dài ra kéo lệch) |
| Δ tương quan L–R | beat stereo (hợp âm trái, hi-hat phải, trống giữa) | ảnh stereo |
| Δ LUFS | như trên | BS.1770 (`key/loudness`) — chỉ để tham khảo, xem B2 |
| Nhiễu so với lý tưởng | nhiễu trắng | công suất bản ra / lý tưởng (giữ nguyên khi hạ tone; × 0,97/β khi nâng tone vì phải bỏ phần trên Nyquist/β) |

## Bảng hiện tại (B3, 2026-10-26)

`beatlab eval --algo pv,pv-b2,pv-basic,signalsmith` (Mac M1, Release, 48 kHz).
- **pv** = `PitchShifter` mặc định: khoá pha identity + chung pha stereo + giữ transient, FFT 4096 /
  hop 1024.
- **pv-b2** = khoá pha, chưa giữ transient (B2, `transients = false`).
- **pv-basic** = phase vocoder cơ bản B1 (mốc dưới, `phaseLock = false`).
- **signalsmith** = Signalsmith Stretch 1.4.0 `presetDefault` (hộp đen, mốc trên).

| Thuật toán, mức | Cao độ sin (cent) | Cao độ hoạ âm (cent) | Ngoài hoạ âm: nốt (dB) | Ngoài hoạ âm: hợp âm (dB) | Trước onset: trung vị / tệ nhất (dB) | Thời gian lên +ms | Dội sau đỉnh (dB) | Lệch onset (ms) | Δ tương quan L–R | Δ LUFS | Nhiễu so với lý tưởng (dB) | × thời gian thực |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gốc (chưa đổi) | 0.00 | 0.00 | -60.0 | -65.1 | -161.3 / -53.0 | 1.31 | -29.1 | 0.00 | 0.902 | -20.92 | 0.00 | — |
| pv -6 | 0.00 | 0.00 | -59.7 | -37.6 | -60.8 / -46.9 | 0.40 | -21.8 | 0.04 | 0.008 | -1.24 | -1.40 | 133 |
| pv -4 | 0.00 | 0.00 | -59.4 | -39.7 | -64.6 / -49.4 | 0.23 | -25.2 | 0.06 | 0.001 | -0.74 | -1.08 | 125 |
| pv -2 | 0.00 | 0.00 | -59.6 | -44.6 | -68.2 / -47.9 | 0.04 | -25.8 | 0.12 | 0.002 | -0.44 | -0.76 | 116 |
| pv +2 | 0.00 | 0.00 | -59.4 | -45.4 | -77.2 / -52.3 | -0.12 | -28.1 | 0.12 | 0.003 | -0.23 | -0.58 | 98 |
| pv +4 | 0.00 | 0.00 | -67.4 | -41.1 | -75.5 / -50.0 | -0.23 | -25.8 | 0.12 | 0.003 | -0.30 | -0.67 | 89 |
| pv +6 | 0.00 | 0.00 | -77.7 | -41.4 | -75.8 / -48.2 | -0.23 | -18.9 | 0.10 | 0.002 | -0.35 | -0.75 | 81 |
| pv-b2 -6 | 0.00 | 0.00 | -59.7 | -37.6 | -21.0 / -18.2 | 17.42 | -9.2 | 16.52 | 0.005 | -1.45 | -1.40 | 177 |
| pv-b2 -4 | 0.00 | 0.00 | -59.4 | -39.6 | -24.7 / -21.3 | 17.45 | -21.0 | 12.81 | 0.002 | -0.90 | -1.08 | 163 |
| pv-b2 -2 | 0.00 | 0.00 | -59.6 | -44.7 | -29.2 / -26.4 | 10.34 | -25.4 | 9.10 | 0.000 | -0.42 | -0.76 | 150 |
| pv-b2 +2 | 0.00 | 0.00 | -59.4 | -45.1 | -29.1 / -26.3 | 7.57 | -23.8 | 9.06 | 0.002 | -0.18 | -0.58 | 121 |
| pv-b2 +4 | 0.00 | 0.00 | -67.4 | -41.1 | -25.9 / -21.6 | 9.86 | -17.8 | 9.88 | 0.002 | -0.29 | -0.67 | 107 |
| pv-b2 +6 | 0.00 | 0.00 | -77.7 | -41.4 | -24.9 / -22.3 | 10.06 | -21.8 | 9.77 | 0.002 | -0.38 | -0.75 | 96 |
| pv-basic -6 | 0.00 | 0.00 | -30.2 | -29.0 | -21.7 / -6.2 | 6.95 | -16.2 | 19.56 | 0.819 | -3.84 | -3.46 | 93 |
| pv-basic -4 | 0.00 | 0.00 | -37.3 | -36.4 | -25.6 / -9.1 | 8.82 | -21.2 | 19.56 | 0.671 | -3.93 | -3.43 | 85 |
| pv-basic -2 | 0.00 | 0.00 | -47.5 | -41.9 | -25.1 / -5.9 | 6.62 | -9.6 | 19.56 | 0.530 | -3.34 | -3.28 | 78 |
| pv-basic +2 | 0.00 | 0.00 | -52.7 | -46.6 | -22.7 / -7.2 | 8.12 | -1.7 | 19.56 | 0.704 | -3.28 | -3.14 | 62 |
| pv-basic +4 | 0.00 | 0.00 | -48.2 | -41.9 | -10.3 / -6.8 | 2.11 | -15.0 | 19.56 | 0.830 | -2.86 | -3.12 | 54 |
| pv-basic +6 | 0.00 | 0.00 | -44.8 | -41.7 | -9.4 / -7.4 | 8.51 | -14.7 | 19.56 | 0.940 | -2.83 | -3.04 | 49 |
| signalsmith -6 | 5.56 | 13.22 | -10.6 | -14.6 | -32.8 / -28.1 | 3.33 | -21.1 | 6.10 | 0.001 | -0.78 | -1.02 | 131 |
| signalsmith -4 | 2.64 | 1.57 | -50.6 | -23.9 | -35.6 / -32.3 | 2.54 | -23.1 | 2.81 | 0.000 | -0.51 | -0.80 | 130 |
| signalsmith -2 | 3.50 | 3.90 | -34.6 | -34.7 | -40.4 / -34.1 | 0.76 | -25.0 | 2.12 | 0.000 | -0.27 | -0.47 | 129 |
| signalsmith +2 | 2.08 | 5.67 | -19.4 | -37.2 | -39.7 / -35.9 | 0.95 | -27.7 | 3.25 | 0.002 | -0.11 | -0.37 | 128 |
| signalsmith +4 | 4.24 | 1.37 | -50.1 | -18.9 | -37.7 / -35.0 | 2.12 | -22.1 | 1.96 | 0.003 | -0.13 | -0.59 | 127 |
| signalsmith +6 | 1.41 | 6.73 | -20.9 | -36.1 | -35.3 / -32.9 | 2.11 | -25.4 | 2.81 | 0.003 | -0.18 | -0.66 | 127 |

## B4 — sau lần nghe A/B đầu tiên (2026-10-27)

**Nghe A/B mù** (`tools/stretch/ab.py`, 8 beat dev × ±2/±4/±6, ToneCore B3 so với Signalsmith; chủ dự án
chấm 14 / 48 mục):

| Chiều | ToneCore B3 | Signalsmith |
|---|---|---|
| Hạ tone (−6, −4, −2) | trống nhòe: chủ yếu "không"; méo: "không" / "hơi" | nhòe "rõ" ở 3 / 6 đoạn |
| Nâng tone (+4, +6) | nhòe "rõ" 2 đoạn (+6), méo "rõ" 2 đoạn (Đế Vương +4, Hoa Nở +6) | được chọn hơn 3 / 4 đoạn ở +6 |

**Nguyên nhân 1 — méo = cắt đỉnh.** Đổi pha làm đỉnh vượt 0 dBFS khi beat đã master sát 0 dBFS (15 s,
+4: Đế Vương 1 462 mẫu > 1, đỉnh 1,29; Trống Cơm 8 625 mẫu, đỉnh 1,57). Tầng ra kẹp ±1 → méo, cả trên
iPhone. Sửa: **limiter đỉnh nhìn trước** (`stretch/peak_limiter.*`: trần 0,98, nhìn trước 2 ms, release
60 ms, một hệ số cho cả hai kênh, dưới trần đi thẳng) ngay trong `BeatRenderer` và `beatlab render`
→ 0 mẫu bị cắt ở mọi mức trên 7 beat.

**Nguyên nhân 2 — nhòe / đánh đôi = "neo" cả khung kéo lệch các tiếng sát nhau.** B3 đặt nội dung
±43 ms quanh mỗi onset theo kiểu phát sample (thời gian ×1/β tính từ onset) → tiếng đánh phụ nằm
trong khoảng đó (flam, ghost note, tiếng chính đến sau một tiếng nhẹ) lệch (1/β − 1)·Δ. Ví dụ đo
được: Bạc Phận −6, tiếng chính +22 ms sau một tiếng nhẹ bị đẩy tới +30 ms. Bộ tín hiệu tổng hợp (trống
đứng riêng) không bắt được → thêm phép đo trên beat thật (`tools/stretch/realeval.py`): **onset ma**
(tiếng đánh có ở bản ra mà gốc không có trong ±8 ms, bộ dò độc lập với engine) và **rò trước onset**
(năng lượng cao tần 5–30 ms trước / 0–20 ms sau, so với gốc).

Đã làm:
- **Bộ dò onset mới** (`TransientDetector`): bao biên năng lượng cao tần bước 1 ms, onset khi tăng
  ≥ 8 dB so với 7 ms ngay trước, đỉnh ±5 ms, cách nhau ≥ 10 ms. Bản spectral flux khung 21 ms trước đây
  bỏ sót tiếng đánh nằm trên đuôi tiếng trước (vd. tiếng chính ở Bạc Phận 1,031 s). Trên tín hiệu
  tổng hợp vẫn đúng tuyệt đối (16 / 16, 32 / 32, không onset giả).
- **Thử tách trống** (Driedger, Müller & Ewert 2014; HPSS cuốn chiếu, phần bộ gõ qua OLA 10,7 ms):
  không dùng. Mặt nạ mềm để 14% năng lượng hợp âm lọt sang OLA (tạp phổ −22 dB); mặt nạ có hệ số tách
  κ = 2 thì sạch phần cao độ nhưng thân kick / snare (phổ hẹp) vẫn sang phần hoà âm (25–30%) và mặt nạ
  trên khung 43 ms trải tiếng đánh ra → hai nhánh không triệt tiêu → rò +5 dB. Đã gỡ khỏi engine.
- **Thử neo một phần** khi nâng tone (α = 0,5 độ dời): onset ma 18 → 10 nhưng dồn tiếng rò vào 0,5–8 ms
  ngay trước tiếng đánh (flam, −10…0 dB so với đỉnh — cả hai phép đo cũ đều mù vùng < 5 ms; test
  `KickHasNoPreEchoAtAnyGridOffset` bắt được). Không dùng; giữ neo trọn.

Kết quả hiện tại (`realeval.py`, 7 beat dev × 15 s, có limiter cả hai bên; không tính Chiếc Khăn Gió
Ấm — ballad nhiều onset nhẹ, bộ dò đo nhiễu với mọi bản):

| Mức | Onset ma: ToneCore / Signalsmith / B2 | Rò trước onset so với gốc (dB): ToneCore / Signalsmith / B2 |
|---|---|---|
| −6 | 28 / 7 / 5 | **+0,2** / +2,7 / +7,1 |
| −4 | 30 / 16 / 6 | **−0,3** / +1,6 / +5,4 |
| −2 | 33 / 18 / 2 | **−0,1** / +0,8 / +4,1 |
| +2 | 13 / 11 / 5 | **+0,8** / +1,0 / +4,3 |
| +4 | 14 / 7 / 4 | **+1,2** / +1,7 / +5,1 |
| +6 | 18 / 8 / 2 | **+2,0** / +2,7 / +5,8 |

Đọc bảng:
- Tiếng rò trước onset: ToneCore sắc nhất ở mọi mức.
- Onset ma: ToneCore nhiều hơn Signalsmith. Khi hạ tone, phần lớn là cấu trúc trong một tiếng trống
  (nhát kép, phản xạ sớm) giãn ×1/β như phát chậm sample — đã nghe là ổn (tốt hơn Signalsmith). Ở −2
  nhiều onset ma nằm đúng 10–17 ms sau onset thật (Signalsmith cũng có): bộ dò đo nhận đỉnh ở chỗ khác
  của cùng một tiếng → phép đo này chỉ dùng để so sánh tương đối. Khi nâng tone ToneCore vẫn kém
  Signalsmith → **nghe lại** (trang `testdata/beat-ab-2/`, 8 beat × +4 / +6, cả hai có limiter).

**Nghe lại lần 2** (2026-10-27, 16 / 16 mục, A/B mù, cả hai có limiter):

| Mức | Nhòe trống: không / hơi / rõ — ToneCore · Signalsmith | Méo, rè, phơ: không / hơi / rõ — ToneCore · Signalsmith | Chọn hơn: ToneCore / Signalsmith / như nhau |
|---|---|---|---|
| +4 | 6 / 2 / 0 · 6 / 2 / 0 | 4 / 4 / 0 · 6 / 2 / 0 | 0 / 2 / 6 |
| +6 | 6 / 1 / 1 · 7 / 1 / 0 | 5 / 3 / 0 · 7 / 1 / 0 | 0 / 2 / 6 |

- Mức "rõ" giảm từ 4 / 14 mục (lần 1) xuống 1 / 16 (Trống Cơm +6 nhòe rõ — ngoài khoảng nghiệm thu ±4).
- +4: không mục nào "rõ"; nhòe ngang Signalsmith; "hơi méo" nhiều hơn (Bạc Phận, Hoa Nở, Duyên Phận,
  Trống Cơm; Signalsmith hơi méo ở Bạc Phận, Hoa Nở).
- "Hơi méo" không do limiter: tỉ lệ mẫu vượt 0,98 trước limiter ngang nhau giữa hai thuật toán (Trống
  Cơm 0,66% / 0,54%, các beat khác ≤ 0,17%). Nhiều khả năng là tiếng "phơ" của phase vocoder ở phần có
  cao độ khi giãn ×β > 1.
- **Quyết định** (chủ dự án, 2026-10-27): chốt chất lượng hiện tại, sang nghe trên iPhone. "Hơi méo"
  khi nâng tone để lại cho M9 (hướng thử: cửa sổ ngắn hơn / đổi cửa sổ quanh tiếng trống khi nâng).

**Tốc độ sau B4** (bộ dò onset mới + limiter; `beatlab render`, Mac M1 Release, 8 beat dev cả bài
200–369 s): nâng +4 **107–110×**, hạ −4 **153–167×** thời gian thực → bài 4 phút 2,2 s / 1,5 s (ngân sách
iPhone ≤ 4 s). Nâng tone chậm hơn vì giãn thời gian ×β > 1 (nhiều khung hơn).

**RAM** (tính): bản đổi tone lưu int16 stereo → bài 4 phút thêm 46 MB (chỉ tiêu ≤ 50 MB), bài 6 phút
71 MB. Bộ nhớ khối chưa render chưa được ghi nên hệ điều hành chưa cấp trang thật → lúc chuyển giữa hai
mức, bản cũ (đã render) và bản mới (mới vài khối) gần như không chồng nhau. Số đo thật: dòng "RAM" ở
Phòng thử trên iPhone.

## B3 — giữ transient

**Vì sao trống nhòe.** Tiếng đánh ở thời điểm gốc t0 nằm trong mọi khung phân tích j có
a_j < t0 < a_j + N (3–6 khung), ở vị trí d_j = t0 − a_j. Sau giãn + resample, bản sao của nó trong
khung j rơi vào t0 + (d_j − N/2)·(1/β − 1) — mỗi khung một chỗ → nhòe ±N/2·|1/β − 1| (±17 ms ở ±6).
Khoá pha (B2) còn làm nặng thêm: các vùng phổ xoay khác góc nhau nên ngay trong một khung tiếng đánh
cũng bị trải ra (thời gian lên +7…17 ms).

**Cách làm** (`stretch/transient_detector.*`, bước 2b trong `pitch_shifter.h`):
1. Dò onset nhìn trước trên L + R: spectral flux log biên độ (khung 21 ms, bước 5 ms), đỉnh cục bộ
   ±30 ms vượt 1,5 × trung vị ±100 ms; thời điểm tinh chỉnh trong miền thời gian (điểm có năng lượng
   cao tần 2 ms sau / 10 ms trước lớn nhất). Giữ onset chỉ khi phần năng lượng **mới** (nhấn cao tần
   theo k) ≥ 70% — loại đỉnh flux giả trong đuôi kick (thân kick quét 150 → 50 Hz) mà vẫn giữ mọi tiếng
   đánh thật (đo: thật 0,90–1,00, giả 0,00–0,54).
2. Mỗi onset: mức trội g_k từng bin = công suất ngay sau / ngay trước onset (tăng ≤ 3 dB → 0, ≥ 9 dB
   → 1), bằng cửa sổ Tukey dốc 1 ms ở phía onset.
3. Ở mọi khung chứa onset, bin trội dùng pha tuyến tính θ_k = ω_k·Δs_j, Δs_j = (d_j − N/2)·(1 − β):
   bản sao của mọi khung trùng nhau tại onset (tiếng đánh được "phát" như một sample: cao độ ×β, đuôi
   dài ×1/β). Bin nền giữ khoá pha; khi onset ra khỏi khung, các bin đó tiến pha tiếp từ góc vừa dùng.

**Hai lỗi đã gặp và sửa** (test `KickHasNoPreEchoAtAnyGridOffset` chặn tái phát):
- Phân loại bin bằng cửa sổ Hann bắt đầu đúng tại onset: Hann ≈ 0 ở mép nên **xoá mất tiếng click
  < 1 ms** → chỉ 46 / 2049 bin của một tiếng kick được căn → các bin cao tần vẫn nhòe, rò −7…−26 dB
  trong 8 ms trước tiếng đánh tuỳ vị trí so với lưới khung. Sửa: cửa sổ Tukey dốc 1 ms → 2049 / 2049
  bin, rò còn −76…−85 dB (riêng 0,5 ms cuối −43 dB: dao động vốn có của nhân sinc).
- Sàn chống chia 0 khi tính độ tăng đặt ở −90 dB so với bin mạnh nhất: thân kick trầm mạnh hơn từng bin
  cao tần của tiếng click > 90 dB → độ tăng bị tính thấp. Sửa: sàn −140 dB.

**Kết quả** (bảng trên, −6…+6): trước onset trung vị −61…−77 dB (B2 −21…−29), tệ nhất −47…−52 dB;
thời gian lên −0,2…+0,4 ms (B2 +7,6…17); dội cao tần −19…−28 dB (gốc −29, B2 −9…−25); lệch onset
0,04–0,12 ms (B2 9–17 ms). Tốt hơn Signalsmith ở mọi mức và mọi chỉ số trống, trừ dội ở +6 (−18,9 so
với −25,4 dB của Signalsmith — vẫn đạt chỉ tiêu −12). Các chỉ số phần có cao
độ, stereo, nhiễu không đổi. Bộ dò: trống 16 / 16 tiếng (lệch ≤ 0,2 ms), beat 32 / 32, không onset
giả trên âm ngân / nhiễu.

**Beat thật** (30 s từ giây 60, onset dò độc lập trên bản gốc bằng flux năng lượng cao tần 2 ms):
năng lượng cao tần 5–30 ms trước onset / 0–20 ms sau, trung vị — càng thấp càng sắc:

| Beat | Gốc | pv −4 / +4 | pv-b2 −4 / +4 | Signalsmith −4 / +4 |
|---|---|---|---|---|
| Bạc Phận remix (6,3 onset/s) | −19,8 | **−18,7 / −18,2** | −12,7 / −12,8 | −15,1 / −16,3 |
| Duyên Phận, bolero (3,9 onset/s) | −27,4 | **−27,5 / −25,3** | −13,9 / −13,7 | −20,7 / −22,8 |
| Em Gái Mưa, ballad (0,1 onset/s) | −13,7 | −13,8 / −14,5 | −13,5 / −12,6 | −13,3 / −13,8 |
| Lạc Trôi, nhạc trẻ (1,6 onset/s) | −11,1 | −10,5 / −10,8 | −9,2 / −9,1 | −10,5 / −10,6 |

**Tốc độ**: bài 4 phút 44,1 kHz ở +4 / +6: 2,78 / 2,97 s trên M1 (B2 2,37 s) — thêm bộ dò onset và
căn pha (tính e^{iω_k·Δs} bằng nhân dồn số phức, không gọi sincos từng bin). `BM_PitchShifter` 48 kHz:
−6 135×, +4 91×, +6 83× thời gian thực.

## B2 — khoá pha identity + chung pha stereo

(Số "dội" và "lệch onset" trong phần này theo định nghĩa cũ; bảng hiện tại đã đo lại theo định nghĩa
mới.)

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

## Chỉ tiêu (chốt ở B3)

| Chỉ số | Chỉ tiêu | pv (B3) | Signalsmith |
|---|---|---|---|
| Cao độ | ≤ 1 cent | ✅ 0,00 | ❌ 1,4–13 |
| Căn thời gian (cục sin) | ≤ 0,5 ms | ✅ 0,00 | — |
| Âm lượng phần có cao độ | ±0,1 dB | ✅ ±0,02 | — |
| Ngoài hoạ âm (nốt / hợp âm) | ≤ −40 / −35 dB | ✅ −59 / −38 | — (lệch cao độ) |
| Δ tương quan L–R | ≤ 0,02 | ✅ ≤ 0,008 | ✅ |
| Nhiễu so với lý tưởng | ≥ −1,5 dB | ✅ −0,6…−1,4 | ✅ −0,4…−1,0 |
| LUFS beat thật so với Signalsmith | ≤ 0,5 LU | ✅ ≤ 0,41 | — |
| Trước onset (trung vị / tệ nhất) | ≤ −30 / −25 dB | ✅ −61 / −47 | ✅ −33 / −28 |
| Thời gian lên | ≤ +2 ms ở ±1…4, ≤ +3 ms ở ±5…6 | ✅ ≤ +0,4 | ✅ ≤ +3,3 |
| Dội cao tần (gốc −29) | ≤ −12 dB | ✅ ≤ −18,9 | ✅ ≤ −21 |
| Lệch onset | ≤ 1 ms | ✅ ≤ 0,12 | ❌ 2,0–6,1 |
| Kick ở mọi vị trí so với lưới khung | ≤ −30 dB trong 0,5–8 ms trước | ✅ ≤ −34 | — |
| Tốc độ (M1, 48 kHz) | ≥ 80× (ngân sách iPhone ≤ 4 s / bài 4 phút) | ✅ 83–135× | 130× |

Chỉ tiêu tốc độ hạ từ 100× xuống 80× ở B3: luồng nền render đi trước đầu phát nên người dùng chỉ chờ
đoạn đầu, tổng thời gian chỉ ảnh hưởng pin. Nếu đo trên iPhone vượt 4 s / bài: FFT nhanh hơn (vDSP /
PFFFT). Bỏ chỉ tiêu "Δ LUFS ≤ 0,5 trên beat tổng hợp" của B1 (xem B2).
