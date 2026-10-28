# M4 — Đổi tone beat ±6, tự cập nhật key chỉnh giọng (kế hoạch, đã duyệt 2026-10-26)

> Soạn 2026-10-26. Dựa trên CLAUDE.md mục 3.5, 3.2, 9 và `docs/plan.md` mục 3.5, rủi ro 4.
> Điều kiện hoàn thành M4 (spec): **không méo, không nhòe trống khi nghe A/B**.

---

## Báo cáo M4 (2026-10-28 — xong)

**Đã xong**
- Đổi tone beat ±6 nửa cung, giữ tempo: phase vocoder khoá pha identity + chung pha stereo, giữ
  transient bằng căn pha tuyến tính quanh onset, resample sinc–Kaiser, limiter đỉnh nhìn trước.
- Render ở luồng nền từ vị trí đang phát, chuyển bằng crossfade 20 ms, tua vào vùng chưa render thì
  ưu tiên vùng đó; audio thread chỉ đọc PCM (không cấp phát, TSan sạch).
- Key chỉnh giọng tự cộng số nửa cung khi bản mới bắt đầu được nghe; app hiện tone đang nghe, nhớ
  mức tone theo bài. C API 0.5, binding Dart 0.5, ADR-004.

**Số đo**
| Mục | Chỉ tiêu | Mac M1 | iPhone |
|---|---|---|---|
| Bấm tone → nghe tone mới | ≤ 300 ms | 7 ms | 26–34 ms |
| Render cả bài 4 phút | ≤ 4 s | 1,5 s (hạ) / 2,2 s (nâng) | 2,7–3,1 s (bài 3,5–4 phút) |
| RAM thêm, bài 4 phút | ≤ 50 MB | 46 MB (tính) | cả app 167–185 MB, đỉnh 269–291 MB (gồm phân tích bài) |
| Nghe A/B mù ±2…±4 | không nhòe / méo "rõ" | hạ tone tốt hơn Signalsmith; nâng: 0 "rõ", "hơi méo" 4 / 8 | chủ dự án: ổn (AirPods, 2026-10-28) |
| Mẫu bị cắt sau đổi tone | 0 | 0 (7 beat × 6 mức) | — |
| Test | xanh | 217 engine + 16 binding + 28 app; ASan / UBSan / TSan sạch | 217 / 217 trong iOS Simulator (Xcode 26) |
| Vị trí phát sau khi đổi tone | đều theo đồng hồ | — | ×1.00, nhảy lùi 0, khựng 0, xrun 0 |

**Vấn đề còn lại**
- Nâng tone (+4…+6) "hơi méo / phơ" ở vài beat, kém Signalsmith một chút; Trống Cơm +6 nhòe rõ → M9.
- RAM tăng theo độ dài bài (6 phút ≈ 71 MB) — nếu Android tầm trung căng: chỉ giữ cửa sổ quanh đầu
  phát (rủi ro 3).
- Chưa cache bản render ra đĩa (đổi tone lại phải render lại, ~2 s).
- Lúc chuyển sang bản mới, nếu khối kế tiếp chưa kịp render thì vị trí phát khựng 1 block (~10 ms);
  chỉ thấy khi render chậm (bản ASan), máy thật render nhanh hơn thời gian thực ~80 lần.
- Lần nghe đầu trên iPhone (2026-10-28) báo "vang / đánh đôi, về Gốc cũng bị" ở +2: không tái hiện
  được — bản engine 48 kHz khớp bản gốc về bao biên, không có tiếng dội cố định (tự tương quan bao
  biên 15–300 ms ngang Signalsmith); lần thử sau nghe ổn. Theo dõi tiếp khi nghiệm thu M5.

**Cách tự kiểm tra**: mục "Cách bạn tự kiểm tra khi xong M4" ở phần 5.

---

## Tiến độ

- [x] **B1** (2026-10-26): `PitchShifter` (`engine/src/stretch/`, phase vocoder cơ bản + resample
  sinc–Kaiser, render từ vị trí bất kỳ, chia khối không đổi kết quả, mức 0 đúng từng mẫu); bộ tín
  hiệu tổng hợp + chỉ số (`tests/util/stretch_eval.*`: cao độ, ngoài hoạ âm, trước onset, thời gian
  lên, dội sau đỉnh, lệch onset, tương quan L–R, LUFS); `beatlab render|eval|suite`; mốc Signalsmith
  Stretch 1.4.0 (hộp đen, `tools/stretch/fetch_signalsmith.sh`); trang nghe mù `tools/stretch/ab.py`.
  Số đo mốc dưới / mốc trên và chỉ tiêu đề xuất: `docs/stretch-results.md`. Phát hiện: Signalsmith
  lệch cao độ 1–13 cent; phase vocoder cơ bản cao độ đúng nhưng trống nhòe, mất stereo, nhỏ đi
  3–4 dB, lệch thời gian (1 − β)·~35 ms (test căn thời gian để tắt tới B2); render bài 4 phút 5,0 s
  trên M1 (cần nhanh 2–3 lần ở B2).
- [x] **B2** (2026-10-26): khoá pha identity (Laroche & Dolson) + chung pha stereo (góc xoay tính
  trên L + R, áp cho mọi kênh). Căn thời gian và âm lượng phần có cao độ đúng tuyệt đối (±0,00 ms,
  ±0,02 dB; test căn thời gian đã bật), tạp phổ nốt −59 dB / hợp âm −38 dB, ảnh stereo lệch ≤ 0,005,
  nhiễu hụt ≤ 1,4 dB (Signalsmith ≤ 1,0). Giữ hop 1024 (512 tốt hơn ~2 dB nhưng chậm gấp đôi). Tăng
  tốc: bài 4 phút 2,37 s trên M1 (B1 5,0 s), 96–176× thời gian thực. Bỏ chỉ tiêu LUFS cũ (K-weighting
  đổi theo cao độ; trên beat thật pv cách Signalsmith ≤ 0,41 LU). Trống còn nhòe (khoá pha còn làm
  tiếng đánh chậm hơn B1) → B3. Chi tiết: docs/stretch-results.md.
- [x] **B3** (2026-10-26): giữ transient — dò onset nhìn trước (`stretch/transient_detector.*`:
  spectral flux + tinh chỉnh miền thời gian + cổng năng lượng mới), bin trội của mọi khung chứa onset
  dùng pha tuyến tính để mọi bản sao trùng nhau tại onset. Trước onset −61…−77 dB (B2 −21…−29),
  tiếng đánh lên +0,4 ms (B2 +17), lệch onset ≤ 0,12 ms — đạt mọi chỉ tiêu trống, tốt hơn Signalsmith
  ở mọi mức (trừ chỉ số dội ở +6: −18,9 so với −25,4 dB, vẫn đạt chỉ tiêu −12). Beat thật: năng lượng trước onset cách bản gốc 0–2 dB (Signalsmith 3–7 dB). Sửa 2 lỗi
  phân loại bin (cửa sổ xoá tiếng click, sàn quá cao). Bài 4 phút 2,8–3,0 s trên M1. Không cần B4.
  Chi tiết: docs/stretch-results.md.
- [x] **B4** (2026-10-27, mở lại sau nghe A/B lần 1 — hạ tone tốt hơn Signalsmith, nâng tone nhòe /
  méo ở vài đoạn): (1) méo = cắt đỉnh sau đổi pha → limiter đỉnh nhìn trước trong render (0 mẫu bị cắt);
  (2) nhòe = neo cả khung kéo lệch các tiếng sát nhau + bộ dò bỏ sót → bộ dò onset mới (năng lượng cao
  tần bước 1 ms); phép đo trên beat thật `tools/stretch/realeval.py`. Đã thử và bỏ: tách trống HPSS +
  OLA, neo một phần khi nâng tone. Nghe lại lần 2 (+4 / +6, 16 mục): không còn mục "rõ" ở +4, nhòe
  ngang Signalsmith, "hơi méo" 4 / 8 đoạn (Signalsmith 2 / 8), 12 / 16 "như nhau" → chủ dự án chốt chất
  lượng hiện tại; "hơi méo" khi nâng tone để lại M9. Chi tiết: docs/stretch-results.md.
- [x] **B5** (2026-10-27): engine đổi tone beat — `TransposedTrack` (khối 4096 frame, cờ atomic,
  int16 dư 6 dB), `BeatRenderer` (render nền từ vị trí đang phát, từng bước 8 khối, ưu tiên chỗ vừa
  tua, nối lượt bằng crossfade), `BeatPlayer` viết lại (chờ khối, crossfade 20 ms, fade ra trước khối
  thiếu, sở hữu shared_ptr ở luồng nền). C API 0.5: `tc_set_transpose / tc_get_transpose /
  tc_get_applied_key`, `TC_EVENT_TRANSPOSE_READY`, `tc_playback_state` thêm 3 trường (nhận struct
  0.4). Key chỉnh giọng tự cộng số nửa cung khi bản mới bắt đầu được nghe. Mac M1 Release, beat thật:
  bấm đổi tone giữa bài → nghe sau 7 ms, render cả bài 2,2–3,3 s. 212 test (thêm 14), TSan / ASan /
  UBSan sạch, audio thread không cấp phát. ADR-004.
- [x] **B6** (2026-10-27): binding Dart 0.5 (ffigen sinh lại; `ToneCoreEngine.transpose`,
  `appliedKey`, `PlaybackState.transpose / audibleTranspose / transposeProgress / transposePending`,
  sự kiện `transposeReady`) — 16 test binding. Phòng thử: nút tone beat − / + (to, chặn ±6), "Đang đổi
  tone…" / "Đang chuẩn bị cả bài: N%", tone hiển thị = tone đang nghe kèm "beat +2"; chọn tone tay
  khi beat đã đổi thì quy về tone gốc trước khi giao engine; nhớ tone theo bài (`song_prefs.json`,
  khoá tên + kích thước file, không gắn phiên bản engine) và tự đặt lại khi mở bài — 24 test app.
- [x] **B7** (2026-10-27/28): Phòng thử hiện số đo trên máy thật — "Đổi tone beat: nghe sau … ms ·
  cả bài … s", "Sau khi đổi tone: vị trí ×… · nhảy lùi … · khựng …" (`playback_watch.dart`),
  "Phân tích bài: … s", "RAM: … MB (đỉnh … MB)"; 28 test app. Tốc độ sau B4 trên M1: nâng
  107–110×, hạ 153–167×. iPhone (AirPods): nghe tone mới sau 26–34 ms, render cả bài 2,7–3,1 s,
  phân tích bài 1,0–1,2 s. Điều tra báo lỗi "lặp / vang" ở lần nghe đầu: vị trí phát trên máy đều
  (×1.00), engine đúng ở mọi lần dựng lại trên Mac (thời gian thực, khối 256–4096, phân tích song
  song, dọn bộ nhớ, ASan / MallocScribble); thêm test nội dung đúng chỗ (`EveryNoteInRightPlace`,
  `KeepsSongPositionAndReturnsToOriginalExactly`) và CI chạy toàn bộ test trong iOS Simulator +
  macos-26 (Xcode 26 như bản IPA). Thử "splice" (chỉ dời đoạn ngắn quanh tiếng đánh): tệ hơn trên
  beat thật → bỏ. Chủ dự án nghe lại: ổn → M4 xong.

## Các điểm cần quyết (2026-10-26: duyệt theo đề nghị)

1. **Kiến trúc: render ở luồng nền, bắt đầu từ vị trí đang phát.** Khi bấm tone ±, luồng nền
   render bản beat mới từ đầu phát trở đi, mỗi lần một đoạn. Vì luồng nền chạy nhanh hơn thời gian
   thực vài chục lần, nó luôn đi trước đầu phát. Sau ~0,1–0,3 s, audio thread chuyển sang bản mới
   bằng crossfade 20 ms. Render xong tới cuối bài thì quay lại render phần đầu bài, để tua lùi
   không phải chờ. Audio thread chỉ đọc PCM như hiện nay, cộng thêm crossfade.
   - Lý do: (a) audio thread gần như không tốn thêm CPU. Đây là cách giảm rủi ro 4 đã ghi trong
     `docs/plan.md` (CPU, nhiệt, xrun trên máy tầm trung khi M5 thêm FX và reverb). (b) Tiết kiệm
     pin: mỗi mức tone chỉ render một lần cho cả bài (đúng ý "render trước cả bài" ở mục 3.5).
     (c) Không bị ràng buộc thời gian thực, nên dùng được thuật toán nhìn trước (dò transient chính
     xác hơn). (d) Dùng lại được cho xuất bài ở M7.
   - Đổi lại: RAM thêm một bản beat (bài 4 phút stereo 48 kHz int16 ≈ +46 MB). Tua tới vùng chưa
     render (hiếm, chỉ trong vài giây đầu sau khi đổi tone) thì beat chờ ~0,1–0,3 s. Trong lúc chờ,
     vị trí đứng yên, giống lúc đang tải.
   - Phương án khác: phase vocoder chạy streaming ngay trong audio thread. Đổi tone tức thì, không
     tốn RAM, nhưng tốn CPU liên tục và dồn cục mỗi hop (FFT 4096 × 2 kênh), trên đúng luồng đang
     căng nhất. Phải "mồi" lại pipeline mỗi lần tua, nên CPU tăng vọt ngay trong callback.
   - **Đề nghị**: render nền.
2. **Mốc so sánh Signalsmith Stretch (MIT).** Chỉ dùng trong công cụ so sánh A/B, như một hộp đen
   (gọi API công khai, không đọc mã bên trong, đúng nguyên tắc clean-room). Không link vào
   `libtonecore`, không ship. Ghi vào `docs/licenses.md` với ghi chú "chỉ dùng trong công cụ".
   Có mốc này mới biết bản tự viết đang ở đâu so với một thư viện tốt. **Đề nghị**: có.
3. **Cách nghe nghiệm thu A/B.** 8 beat từ tập dev (chọn bài có trống rõ, đủ thể loại), mỗi beat
   lấy một đoạn 30 s, ở các mức −6, −4, −2, +2, +4, +6. Trang nghe mù, mở ngay trên máy (không
   upload vì là nhạc có bản quyền): gốc / ToneCore / Signalsmith, không ghi tên. Bạn chấm từng đoạn
   theo hai câu hỏi: trống có nhòe không, có méo hoặc rè không. Thêm phần nghe trực tiếp trên iPhone
   ở Phòng thử.
   - **Đạt** khi: ở ±1…±4 không nghe thấy nhòe hay méo. Ở ±5…6, màu tiếng nhạc cụ đổi là điều tự
     nhiên khi đổi cao độ (vật lý, không phải lỗi), nhưng vẫn không được có tiếng "phơ", vang ảo
     hay trống đánh đôi.
   - **Đề nghị**: như trên.
4. **Ý nghĩa của key.** `tc_set_key` và timeline (M3) luôn là tone của **beat gốc**. Engine tự cộng
   số nửa cung đã đổi khi chỉnh giọng và khi so lưới cao độ. Hàm mới `tc_get_applied_key` trả tone
   đang nghe, để app hiển thị chữ to. Đổi tone thì phát `TC_EVENT_KEY_CHANGED`. Cách này khớp với
   timeline: mốc thời gian giữ nguyên, chỉ cộng nửa cung. App chỉ cần nhớ "bài này hát ở tone +2".
   **Đề nghị**: như trên.
5. **Bộ nhớ đệm.** RAM giữ bản gốc và bản đang dùng. Chưa lưu bản render ra đĩa ở M4. App nhớ mức
   tone đã chọn theo từng bài và tự render lại khi mở bài. **Đề nghị**: như trên.

---

## 1. Kết quả đầu ra

- Trong engine: đổi tone beat −6…+6 nửa cung, giữ nguyên tempo và vị trí. Mức 0 phát đúng bản gốc,
  từng mẫu như cũ. Bấm liên tiếp nhiều lần thì huỷ lần render trước và chỉ giữ lần cuối.
- Chỉnh giọng, timeline key và đồng hồ cao độ tự theo tone mới. Tuning của beat giữ nguyên, vì đổi
  đúng 2^(n/12) không làm lệch tuning.
- `beatlab` (CLI): render file → WAV ở mức n, chọn thuật toán và tham số, tính các chỉ số khách
  quan. Thêm công cụ dựng trang nghe A/B mù.
- Trong app: Phòng thử có nút tone − / +, hiện "Tone: Si thứ (Bm) · +2", có thanh tiến độ render.
- Tài liệu: `docs/stretch-results.md` (số đo), ADR-004 (kiến trúc đổi tone), `docs/references.md`,
  `docs/patent-review.md` (mục M4).

---

## 2. Thuật toán (render ở luồng nền)

**Đổi cao độ = kéo giãn thời gian bằng phase vocoder (hệ số α = 2^(n/12)) + resample 1/α.** Độ dài và
vị trí giữ nguyên. Độ trễ thuật toán được bù để bản mới khớp từng mẫu với bản gốc.

1. **STFT** cửa sổ Hann 4096 mẫu (85 ms ở 48 kHz, bin 11,7 Hz, đủ tách nốt bass). Hop tổng hợp
   1024 hoặc 512 mẫu, chọn bằng đo ở B2.
2. **Khoá pha "identity"** (Laroche & Dolson 1999): tìm đỉnh phổ. Pha của đỉnh tiến theo tần số tức
   thời. Các bin quanh đỉnh giữ đúng chênh pha với đỉnh như trong phổ phân tích. Cách này giảm tiếng
   "phơ" (phasiness) của phase vocoder thường.
3. **Giữ transient** (Röbel 2003; Duxbury, Davies & Sandler 2002): dò onset bằng spectral flux có
   nhìn trước. Ở khung có transient, đặt lại pha theo phổ phân tích, để tiếng trống giữ nguyên hình
   sóng thay vì bị trải ra (nhòe, "trống đánh đôi").
4. **Giữ ảnh stereo**: tính độ xoay pha một lần cho mỗi bin, áp giống nhau cho cả L và R. Chênh
   pha giữa hai kênh được giữ, nên không bị mất ảnh stereo. Beat mono (L = R) ra vẫn đúng L = R.
5. **Resample** 1/α bằng sinc–Kaiser (`dsp/resampler`, đo tốc độ và tối ưu nếu chậm).
6. **Ứng viên thứ hai** (chỉ làm nếu 1–5 chưa đạt): PGHI, phase vocoder không cần dò đỉnh và
   transient (Průša & Holighaus 2017). Thử thêm cách tách HPSS (Driedger, Müller & Ewert 2014):
   phần hoà âm qua phase vocoder, phần bộ gõ xử lý riêng bằng cửa sổ ngắn.
7. **Nối đoạn**: các đoạn render từ những điểm khác nhau (từ đầu phát, rồi quay lại đầu bài) được
   nối bằng crossfade, có pre-roll, trên vùng chưa phát.

---

## 3. C API 0.5 và engine

```c
/* Đổi tone beat (nửa cung, −6..+6). Render ở luồng nền; nghe thấy sau ~0,1–0,3 s. */
TC_EXPORT tc_result tc_set_transpose(tc_engine* engine, int32_t semitones);
TC_EXPORT tc_result tc_get_transpose(tc_engine* engine, int32_t* out_semitones);
/* Tone đang áp dụng cho chỉnh giọng = tone beat gốc (tay hoặc timeline) + số nửa cung đã đổi. */
TC_EXPORT tc_result tc_get_applied_key(tc_engine* engine, int32_t* out_root, int32_t* out_scale);
```

- `tc_playback_state` có thêm `transpose` và `transpose_progress` (0..1, phần bài đã render).
- Sự kiện mới `TC_EVENT_TRANSPOSE_READY` (code = số nửa cung, value = thời gian render, ms).
  `TC_EVENT_KEY_CHANGED` phát cả khi đổi tone.
- Engine: `BeatRenderer` chạy trên `Worker`. Bản render dùng chung một bộ đệm, có bản đồ các khối
  đã xong (cờ atomic từng khối ~85 ms). Audio thread chỉ đọc các khối đã công bố. Thay bản và dọn
  bản cũ dùng hàng đợi SPSC như `BeatPlayer` hiện nay. **Audio thread không cấp phát, không khoá**
  (thêm ca vào `rt_alloc_test`, TSan sạch).
- Binding Dart: `setTranspose`, `transpose`, `appliedKey`, sự kiện. Test như ADR-001.

---

## 4. Công cụ, test, số đo

**Unit test (CI, tín hiệu tổng hợp có đáp án):**

| Chỉ số | Tín hiệu | Đề xuất chỉ tiêu |
|---|---|---|
| Cao độ sau khi đổi | sin, âm có hoà âm, ±1…±6 | sai ≤ 1 cent |
| Khớp thời gian | chuỗi click, trống tổng hợp | onset lệch ≤ 1 ms, độ dài đúng từng mẫu |
| Nhòe trống (pre-echo) | kick / snare / hi-hat tổng hợp + hợp âm | năng lượng 5–50 ms trước onset ≤ −30 dB so với đỉnh |
| Độ sắc tiếng đánh | như trên | thời gian lên 10→90% tăng ≤ 2 ms |
| Tạp âm phổ | âm có hoà âm ổn định | năng lượng ngoài các hoà âm đã dịch ≤ −40 dB |
| Stereo | nguồn pan, nguồn L = R | tương quan giữa hai kênh lệch ≤ 0,05; L = R giữ đúng |
| Âm lượng | nhạc tổng hợp | lệch LUFS ≤ 0,5 LU |
| Mức 0 | mọi tín hiệu | đúng từng mẫu với bản gốc |

Các ngưỡng trên là đề xuất ban đầu. Ở B1, mình sẽ đo phase vocoder thường (không khoá pha, không
giữ transient) làm mốc dưới, và Signalsmith làm mốc trên (nếu được duyệt ở điểm 2), rồi chốt
ngưỡng.

**Hiệu năng** (Mac M1 trong CI/bench, iPhone 13 Pro qua chỉ số của app):
- Render cả bài 4 phút ở một mức tone: ≤ 4 s trên iPhone, ở luồng nền.
- Từ lúc bấm đến lúc nghe tone mới: ≤ 300 ms. Tua vào vùng chưa render: chờ ≤ 300 ms.
- Audio thread: CPU không tăng so với M3 (chỉ đọc PCM và crossfade), không xrun trong soak 30 phút
  trên host.
- RAM: thêm ≤ 50 MB cho bài 4 phút.

**Dữ liệu nghe**: beat dev trong `testdata/` (không commit, không đưa lên CI). Trang nghe A/B dựng
ra `testdata/beat-ab/`, chỉ mở trên máy.

---

## 5. Các bước

| Bước | Nội dung | Tuần DSP |
|---|---|---|
| B1 | `beatlab` (render WAV qua dr_wav, chỉ số khách quan), tín hiệu tổng hợp, mốc dưới (phase vocoder thường), mốc Signalsmith (nếu duyệt), trang nghe A/B | 0,5 |
| B2 | Phase vocoder + khoá pha identity + stereo chung pha + resample; chọn hop; test cao độ, thời gian, tạp âm, stereo | 1 |
| B3 | Giữ transient (dò onset nhìn trước, đặt lại pha); test pre-echo và độ sắc; nghe thử trên dev | 1 |
| B4 | *Chỉ khi B3 chưa đạt*: PGHI và/hoặc tách HPSS; chọn bằng số đo + nghe | 0,5 |
| B5 | Engine: `BeatRenderer` nền, bản đồ khối, crossfade, tua, huỷ; C API 0.5; key tự cộng nửa cung; test RT/TSan; ADR-004 | 0,75 |
| B6 | Binding Dart 0.5 + Phòng thử (tone − / +, tone đang nghe, tiến độ, nhớ theo bài) | 0,5 (mobile) |
| B7 | Đo tốc độ và RAM, `docs/stretch-results.md`, dựng trang A/B cho bạn nghe, báo cáo M4 | 0,5 |

Tổng ≈ **3,75–4,25 tuần DSP** + 0,5 tuần mobile (kế hoạch gốc: 4 + 1).

**Cách bạn tự kiểm tra khi xong M4**:
- `ctest --preset host` xanh (gồm các test ở mục 4).
- `beatlab render beat.m4a --semitones 3 -o out.wav` rồi nghe.
- Mở `testdata/beat-ab/index.html`, nghe mù, chấm.
- Trên iPhone: nạp beat, bấm tone +2 khi đang phát, nghe beat đổi tone sau chưa tới nửa giây, tone
  hiển thị và chỉnh giọng đổi theo.

---

## 6. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | **Trống vẫn nhòe** ở mức lớn (±5…6) dù đã giữ transient | B4: PGHI / tách HPSS. So với mốc Signalsmith để biết giới hạn thực tế |
| 2 | **Tiếng "phơ", vang ảo** trên hợp âm và pad kéo dài | Khoá pha identity, thử hop 512. Chỉ số tạp âm phổ |
| 3 | **RAM** bài dài (10 phút ≈ +115 MB) trên Android tầm trung | Nếu đo thấy căng: chỉ giữ cửa sổ quanh đầu phát (vd. ±60 s), tua xa thì render lại |
| 4 | **Luồng nền bị hệ điều hành giảm ưu tiên**, render không theo kịp | Render nhanh hơn thời gian thực ≥ 20 lần; luồng nền QoS "userInitiated" / ưu tiên cao. Chưa có khối thì chờ thay vì phát sai tone |
| 5 | **Chỗ nối đoạn** nghe được | Pre-roll + crossfade, test tổng hợp ở chỗ nối |
| 6 | Bằng sáng chế về phase vocoder đổi cao độ (cuối thập niên 1990) | Ghi vào `docs/patent-review.md`, mô tả cách làm, cần luật sư xác minh số bằng và tình trạng hết hạn |

Việc còn mở của M3 (đo thời gian/RAM phân tích trên iPhone, nhãn BPM nhạc Việt) và nghiệm thu M2
trên iPhone không chặn M4, làm song song khi bạn có máy.
