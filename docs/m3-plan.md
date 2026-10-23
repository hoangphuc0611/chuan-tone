# M3 — Dò tone, BPM, loudness, theo dõi chuyển tone (kế hoạch, đã duyệt 2026-10-22)

> Soạn 2026-10-22. Dựa trên CLAUDE.md mục 3.6, 3.10, 9 và `docs/plan.md` mục 3.6.
> Điều kiện hoàn thành M3 (spec): **điểm MIREX trung bình ≥ 0,85 trên bộ test 50 bài Việt**.

---

## Tiến độ

- [x] **B1** (2026-10-22): khung phân tích `engine/src/key/song_analyzer.*` (giải mã → mono →
  hạ mẫu, tiến độ/huỷ), `keylab analyze|batch`, `tools/keytest` (CSV, chấm MIREX kiểm chéo
  mir_eval, báo cáo, chặn chỉnh tham số trên test), hướng dẫn gán nhãn `tools/keytest/README.md`,
  job CI `keytest`.
- [x] **B2** (2026-10-22): HPSS cuốn chiếu (median trượt có sắp xếp), ước lượng tuning, CQT đa tốc
  độ, chroma bass/treble; `keylab analyze --chroma out.csv`. Số đo (Mac M1, Release, bài tổng hợp
  4 phút stereo 48 kHz): **tổng 0,8 s** (hạ mẫu 0,12 s, HPSS 0,52 s, CQT 0,14 s), RAM đỉnh 87 MB;
  tuning đúng ±3 cent trên test tổng hợp (−45…+49 cent, cả khi có trống + nốt tắt dần); HPSS tách
  hợp âm/tiếng gõ còn sai −12,9 / −16,1 dB.
- [x] **B3** (2026-10-23): dò tone toàn bài (`key/key_detect.*`, `key/key_profiles.*`):
  tương quan Pearson chroma treble + bass với profile xoay vòng, độ tin cậy = softmax × độ rõ tone.
  Profile kk, temperley-kp (đã đối chiếu số liệu với music21), temperley 1999 (chưa đối chiếu),
  diatonic. Test tổng hợp: cả 4 profile đúng 24/24 tone; đầu-cuối có trống + lệch −35 cent đúng.
  **GuitarSet: 0,706** (mặc định tạm temperley-kp / bass 0,25 / L1, chọn bằng quét 24 cấu hình;
  kk mặc định cũ 0,608); phần đệm hợp âm 0,87–0,93. Job CI `key-eval` chặn hồi quy < 0,68.
  Chi tiết: `docs/key-results.md`. *Đổi so với kế hoạch*: chưa có EDMA / Albrecht–Shanahan (cần
  số liệu gốc của bài báo) — B4 thay bằng profile học từ tập dev Việt.
- [x] **B5** (2026-10-23, làm trước B4 vì chưa có dữ liệu): timeline chuyển tone
  (`key/key_timeline.*`) — HMM 24 trạng thái, cửa sổ 6 s / bước 2 s, chuyển ưu tiên tone họ hàng
  (quãng 5, tương đối, song song, nâng 1–2 nửa cung), Viterbi + forward–backward, gộp đoạn < 12 s,
  một đoạn thì dùng tone toàn bài. Test tổng hợp đúng cả Am → A, nâng nửa cung, Am → C → Am
  (biên ≤ 4–6 s); GuitarSet (không chuyển tone): 19/360 bài chuyển giả, điểm timeline mọi bài
  0,702. keytest thêm chỉ số "timeline mọi bài". `keylab --timeline on|off --switch-prob
  --emission-temp --min-segment --window-sec --related-boost`. Chi tiết: `docs/key-results.md`.
- [x] **B6** (2026-10-23): BPM (`key/tempo.*`: onset từ phần bộ gõ, tự tương quan + răng lược,
  prior Ellis 120 BPM σ 1,4) — test tổng hợp 90–140 BPM sai ≤ 1%, GuitarSet Acc1 58% / Acc2 83%;
  LUFS (`key/loudness.*`, BS.1770-4) — khớp bảng hệ số 48 kHz, 5 ca EBU Tech 3341 ±0,1 LU ở
  48 / 44,1 kHz. `keylab --tempo-prior --tempo-octaves`. Cả chuỗi bài 4 phút 1,12 s (M1).
- [x] **B7** (2026-10-23): C API 0.4 — `tc_analyzer_*` (handle riêng, luồng nền, kéo trạng thái,
  huỷ; ADR-003), `TC_ERR_CANCELLED`, `tc_set_tuning` / `tc_get_tuning` (lưới nốt chỉnh giọng theo
  tuning của beat; đồng hồ cao độ đo theo cùng lưới), `tc_set_key_timeline` (engine tự đổi key theo
  vị trí beat kể cả khi tua, `TC_EVENT_KEY_CHANGED`, chọn tay thì tắt). Timeline sang audio thread
  qua hàng đợi SPSC, key override chỉ audio thread ghi → không cấp phát (test RealTime). 179 test,
  ASan/UBSan/TSan sạch.
- [ ] B4, B8, B9

## Các điểm cần quyết (2026-10-22: duyệt theo đề nghị)

Điểm 2–5 chốt theo **đề nghị**: chấm bài chuyển tone theo thời lượng trên timeline, quãng 5 chỉ
tính chiều lên (chuẩn MIREX), BPM Acc2 ≥ 90% và Acc1 ≥ 75%, có làm demo iPhone (B8).
Điểm 1 là thông tin về dữ liệu — **vẫn chờ bạn trả lời** (beat hay bài gốc, đã gán nhãn chưa).

1. **Bộ dữ liệu là beat hay bài gốc?** Beat karaoke (không lời), bài gốc có giọng ca sĩ, hay lẫn cả hai.
   Thuật toán chạy được cả hai, nhưng tập dev phải giống tập test. Cần biết thêm:
   bộ 50 bài đã gán nhãn chưa, ai gán, định dạng file (MP3/M4A/WAV), và lúc nào có ~150 bài dev.
   **Đề nghị**: tuần đầu gửi trước 20–30 bài dev để chỉnh thuật toán trên nhạc Việt thật.

2. **Định dạng CSV và quy ước gán nhãn khi bài chuyển tone.** Đề xuất:

   ```csv
   file,key,mode,modulation,bpm,genre,notes
   bolero/duyen_phan.mp3,A,minor,"95.0:A:major;160.5:A:minor",68,bolero,
   ballad/abc.m4a,C#,major,,76,ballad,beat lệch tuning?
   ```

   - `key`, `mode`: tone **lúc vào bài** (sau intro).
   - `modulation`: các mốc `giây:chủ âm:mode` kể từ mốc đó; bỏ trống nếu không chuyển tone.
   - `bpm`, `genre`: không bắt buộc. Nếu có `bpm` thì chấm được dò BPM.

   **Chấm điểm**: bài không chuyển tone thì so key toàn bài. Bài có chuyển tone thì lấy trung bình
   điểm MIREX theo thời lượng trên timeline. Chỉ tiêu 0,85 là trung bình 50 bài theo cách này.
   Báo cáo in thêm điểm "chỉ tone chính" để tham khảo.
   *Phương án khác*: chỉ chấm tone chính và bỏ qua timeline. Dễ đạt hơn nhưng không kiểm được
   việc theo dõi chuyển tone mà spec yêu cầu.

3. **Lệch quãng 5 tính theo chiều nào?** Theo chuẩn MIREX và `mir_eval` mặc định, chỉ khi tone đoán
   **cao hơn** đáp án một quãng 5 (vd. đáp án C, đoán G) mới được 0,5. Đoán F (thấp hơn quãng 5)
   thì được 0. **Đề nghị**: tính chỉ tiêu theo chuẩn này (chặt hơn), báo cáo kèm điểm khi tính cả
   hai chiều.

4. **Chỉ tiêu BPM.** Spec chưa có số. Đề xuất (nếu CSV có cột `bpm`):
   - Acc2 ≥ 90%: đúng ±4%, chấp nhận gấp đôi/gấp ba hoặc một nửa/một phần ba.
   - Acc1 ≥ 75%: đúng ±4% không chấp nhận nhầm bội số.

   Acc1 quan trọng cho delay đồng bộ BPM ở M5. Bolero hay bị đoán gấp đôi (~130 thay vì ~65).

5. **Có làm demo trên iPhone trong M3 không?** Kế hoạch gốc không có việc mobile ở M3. Đề nghị
   thêm ~3–4 ngày (mục B8): màn Phòng thử tự dò tone khi nạp beat và tự đi theo timeline khi hát.
   Màn "Phân tích bài" đầy đủ vẫn để M6.

---

## 1. Kết quả đầu ra

Một lần phân tích file beat cho ra:

| Thông tin | Dùng ở đâu |
|---|---|
| **Timeline tone** `[{start, end, root, mode, confidence}]` + tone chính | chỉnh giọng tự đổi key theo vị trí bài; UI hiện tone |
| **Tuning** (cent so với La = 440 Hz) | CQT; **chỉnh giọng kéo về lưới của beat** thay vì lưới 440 Hz (M2 đang cố định 440) |
| **BPM** + độ tin cậy + phương án ×2 / ×½ | delay đồng bộ BPM (M5), gợi ý preset |
| **LUFS** tích hợp (BS.1770-4) + đỉnh mẫu | chuẩn hoá âm lượng beat. Mức đích chốt ở M5 khi trộn tiếng |

Ngoài phạm vi M3: tách vocal (M6), model CNN (chỉ làm khi thuật toán cổ điển không đạt, xem mục 5),
fingerprint, kho tone dùng chung.

---

## 2. Thuật toán (offline, chạy ở luồng nền)

```
file ─ giải mã (codec HĐH, đã có) ─┬─ LUFS BS.1770-4 (tần số gốc, mọi kênh)
                                   └─ mono → hạ mẫu ÷4 (48 kHz → 12 kHz, 44,1 kHz → 11025 Hz)
                                        └─ STFT → HPSS (median filter)
                                             ├─ phần hoà âm → CQT 36 bin/quãng 8 → tuning → chroma bass + chroma treble
                                             │     ├─ tone toàn bài: so khớp profile (+ gợi ý chủ âm cuối bài)
                                             │     └─ timeline: điểm từng đoạn 6 s → HMM 24 trạng thái → Viterbi
                                             └─ phần bộ gõ → onset (spectral flux) → autocorrelation + prior → BPM
```

- **Hạ mẫu theo hệ số nguyên** (gốc / round(gốc / 11025)): đủ dải cho chroma (≤ ~4 kHz) và onset,
  CPU/RAM giảm ~4 lần so với 48 kHz. Dùng `Decimator` polyphase đã có. *(B1: đã thử `Resampler`
  tỉ lệ lẻ 48 → 11,025 kHz, chậm gấp ~10 lần: 248 ms so với 26 ms cho 33 s audio, nên đổi.)*
- **HPSS** (Fitzgerald 2010): median theo thời gian ra phần hoà âm, median theo tần số ra phần bộ gõ,
  mặt nạ mềm, ISTFT. Một bước dùng cho cả dò tone (bỏ trống) và BPM (bỏ hoà âm).
  Chạy cuốn chiếu theo khối, không giữ cả spectrogram trong RAM.
- **CQT** (Schörkhuber & Klapuri 2010): kernel phổ thưa theo từng quãng 8 + hạ mẫu dần.
  Dải E1 (41 Hz) – D#7 (2,5 kHz), 36 bin mỗi quãng 8 (3 bin mỗi nửa cung), khung đầu ra ~0,34 s.
- **Tuning** (ý tưởng Harte & Sandler 2005): *B2 đổi cách làm* — lấy đỉnh phổ 400–3000 Hz ngay trên
  các khung STFT hoà âm của HPSS (nội suy parabol, chính xác hơn CQT 3 bin/nửa cung), histogram vòng
  1 cent, lấy **mode** chứ không lấy trung bình (hoạ âm bậc 5, 7 lệch −14, −31 cent kéo lệch trung
  bình). Nhờ vậy chỉ cần **một** lượt CQT, kernel dời sẵn theo tuning.
- **Chroma**: 12 chiều, mỗi nốt = tổng 3 bin CQT, nén log(1 + γE/E_ref) với E_ref = phân vị 95
  công suất khung. Tách **bass** (≤ C3, giảm dần tới C4) và **treble**, vì nốt bass giúp phân
  biệt trưởng/thứ tương đối, chỗ ngũ cung và bolero hay nhầm nhất. Chuẩn hoá theo khung để B3 chọn.
- **Tone toàn bài**: tương quan với 24 profile xoay vòng. So các bộ profile Krumhansl-Kessler,
  Temperley, EDMA (Faraldo 2016), Albrecht-Shanahan (2013) cùng trọng số bass/treble, rồi
  **chọn trên tập dev**. Thêm một đặc trưng tuỳ chọn: chủ âm của hợp âm và nốt bass cuối bài
  (beat Việt thường kết về chủ âm). Chỉ giữ nếu tăng điểm trên dev.
- **Timeline chuyển tone** (Noland & Sandler 2006; Papadopoulos & Peeters 2012): đoạn 6 s, bước 2 s.
  Điểm mỗi đoạn đổi thành xác suất bằng softmax. HMM 24 trạng thái, ma trận chuyển ưu tiên ở lại.
  Khi chuyển thì ưu tiên tone họ hàng: quãng 5, tương đối, và **song song** (bolero hay Am → A ở
  điệp khúc). Viterbi cho đường đi, forward-backward cho độ tin cậy từng đoạn. Gộp đoạn < 12 s vào
  đoạn bên cạnh. Tham số ε (xác suất chuyển) và độ dài tối thiểu chỉnh trên dev.
- **BPM** (Percival & Tzanetakis 2014; Ellis 2007): onset từ log-spectral-flux của phần bộ gõ
  (~172 khung/s), autocorrelation 40–240 BPM, tăng cường bằng các bội số của chu kỳ, nhân prior
  log-Gauss. Tâm prior chỉnh trên dev theo nhạc Việt, có thể khác nhau theo `genre`.
- **LUFS** (ITU-R BS.1770-4): lọc K-weighting (tính hệ số theo tần số lấy mẫu của file), khối 400 ms
  chồng 75%, cổng −70 LUFS tuyệt đối và −10 LU tương đối.

Toàn bộ chạy ngoài audio thread nên được cấp phát bộ nhớ. Có tiến độ 0–1 và huỷ được giữa chừng.
Nguồn đã có sẵn trong `docs/references.md` mục M3. Sẽ thêm Noland & Sandler,
Papadopoulos & Peeters, Albrecht & Shanahan, Brown & Puckette (1992), EBU Tech 3341.

**Ngân sách** (đo và báo cáo ở B9): bài 4 phút phân tích **≤ 5 s trên iPhone 13 Pro**. RAM đỉnh
**≤ 100 MB**, kể cả audio đã giải mã. Android tầm trung đo sau, theo thứ tự nền tảng đã chốt.

---

## 3. C API 0.4 và engine

**Đề xuất ADR-003: phân tích là một handle riêng, kết quả theo kiểu kéo.** ADR-001 từng dự kiến dùng
`NativeCallable.listener` cho việc dài. Đề nghị bỏ cách đó và dùng cùng kiểu kéo như `tc_poll_events`,
vì 3 lý do: không phải quản lý vòng đời callback khi isolate Dart bị huỷ; chạy được khi engine audio
chưa start (lúc nhập thư viện bài); dùng chung cho công cụ dòng lệnh.

```c
typedef struct tc_analyzer tc_analyzer;
tc_result tc_analyzer_create(tc_analyzer** out);
tc_result tc_analyzer_start(tc_analyzer*, const char* path_utf8);   // chạy nền
tc_result tc_analyzer_get_status(tc_analyzer*, int32_t* state, float* progress, int32_t* stage);
tc_result tc_analyzer_cancel(tc_analyzer*);
tc_result tc_analyzer_get_result(tc_analyzer*, tc_analysis_result*); // root, mode, conf, tuning, bpm, lufs…
int32_t   tc_analyzer_get_key_timeline(tc_analyzer*, tc_key_segment* out, uint32_t max);
void      tc_analyzer_destroy(tc_analyzer*);

/* Engine */
tc_result tc_set_tuning(tc_engine*, float cents);                   // lưới nốt của chỉnh giọng
tc_result tc_set_key_timeline(tc_engine*, const tc_key_segment*, uint32_t count);
/* + TC_EVENT_KEY_CHANGED khi engine tự đổi key theo vị trí beat */
```

Tên hàm `tc_analyze_file` trong spec mục 3.9 chỉ là ví dụ. Nhóm `tc_analyzer_*` ở trên thay cho nó.

**Engine đi theo timeline** `[RT]`: timeline được chép vào mảng cấp phát sẵn ở luồng điều khiển rồi
chuyển con trỏ qua SPSC queue. Mỗi block, audio thread so vị trí beat với timeline để đổi key cho
`NoteQuantizer`. Mảng cũ trả về Worker để huỷ. Seek hay đổi key tay đều cập nhật đúng. Đổi key tay
thì tắt chế độ theo timeline cho tới khi gọi lại `tc_set_key_timeline`. Bổ sung test
`RealTime.ProcessDoesNotAllocate` cho đường này.

**Tuning trong chỉnh giọng**: `NoteQuantizer`/`PitchCorrector` nhận độ lệch lưới (cent). Đồng hồ cao
độ cũng đo theo lưới đó.

---

## 4. Công cụ, test, dữ liệu

**`engine/tools/keylab`** (giống `pitchlab`):
- `keylab analyze <file> [--profile …] [--chroma out.csv]` → JSON gồm timeline, tuning, BPM, LUFS.
- `keylab batch <danh sách>` → JSONL, chạy nhiều luồng.

**`tools/keytest/`** (Python, chỉ chạy trên máy dev và CI):
- `validate_csv.py`: kiểm tra CSV (tên tone, mốc thời gian tăng dần, file tồn tại).
- `keytest.py --set dev|test`: chạy keylab rồi chấm MIREX. Có hai bản chấm: tự viết, và
  `mir_eval.key.weighted_score` (MIT) để kiểm chéo. Xuất báo cáo Markdown/CSV gồm: từng bài, điểm
  trung bình, phân loại lỗi (đúng / quãng 5 / tương đối / song song / sai), Acc1/Acc2 cho BPM,
  thời gian chạy.
- `keytest.py sweep --set dev`: quét tham số và profile. **Chỉ chạy trên dev, không bao giờ trên
  test**. Script từ chối `--set test` khi quét. Mỗi lần chạy test đều ghi log để biết đã chạy bao
  nhiêu lần.

**Unit test** (GoogleTest, tín hiệu tổng hợp có đáp án chính xác):
- CQT: sin ở tần số biết trước rơi đúng bin. Sin lệch +30 cent thì tuning đo được trong ±3 cent.
- Chroma: hợp âm tổng hợp nhiều hoạ âm cho đỉnh đúng 3 nốt.
- Tone: vòng hoà âm I–IV–V–I (và i–iv–V–i) ở cả 24 tone, nhiều voicing ngẫu nhiên, tuning lệch
  ±40 cent → đúng 100%. Thêm trống to lẫn vào vẫn đúng (kiểm HPSS).
- Timeline: Am 60 s → A 40 s → biên chuyển tone lệch ≤ 4 s. Bài không chuyển tone → 0 lần
  chuyển giả.
- BPM: pattern trống tổng hợp 60–180 BPM → sai ≤ 1%.
- LUFS: các ca test EBU Tech 3341 tự sinh, vd. sin 1 kHz stereo −23 dBFS → −23,0 ± 0,1 LUFS,
  các ca có cổng. Chạy ở 44,1 và 48 kHz.
- C API: start / tiến độ / huỷ / kết quả / file lỗi. Test Dart tương ứng (ADR-001).

**Dữ liệu**:
- Bộ của bạn (50 test + ~150 dev) để trong `testdata/vn-key/` (đã `.gitignore`). **Không commit
  audio, không đưa lên CI.** File CSV nhãn có thể commit.
- Bộ công khai để làm trong lúc chờ: **GuitarSet** (tone và tempo nằm trong tên file, CC BY 4.0).
  Sẽ kiểm lại license rồi ghi vào `docs/licenses.md` trước khi dùng. Đây là guitar solo, không phải
  nhạc Việt, nên chỉ dùng kiểm tra sơ bộ. Không dùng GiantSteps vì audio là preview Beatport,
  license không rõ.
- CI: thêm job `key-eval` chạy keylab trên GuitarSet và tín hiệu tổng hợp, đặt ngưỡng chặn
  hồi quy. Kết quả trên bộ Việt ghi vào `docs/key-results.md` sau mỗi lần chạy trên máy dev.

---

## 5. Các bước

| Bước | Nội dung | Tuần DSP | Cần dữ liệu Việt? |
|---|---|---|---|
| B1 | Khung phân tích (giải mã → resample → tiến độ/huỷ), `keylab` rỗng, `keytest` + `validate_csv` (kiểm bằng dự đoán giả + mir_eval) | 0,5 | Không |
| B2 | STFT/ISTFT, HPSS, CQT, tuning, chroma bass/treble + unit test | 1 | Không |
| B3 | Tone toàn bài: 4 bộ profile, test tổng hợp 24 tone, chạy GuitarSet | 1 | Không |
| B4 | Chỉnh trên dev Việt: chọn profile, trọng số bass, gợi ý chủ âm cuối bài. **Mốc kiểm**: dev < 0,80 thì báo bạn trước khi đi tiếp (xem rủi ro 2) | 1 | **Có** (dev) |
| B5 | Timeline: HMM/Viterbi + forward-backward, gộp đoạn, test chuyển tone, chấm theo thời lượng | 1 | Có (bài có chuyển tone) |
| B6 | BPM + LUFS + test EBU, chỉnh prior BPM trên dev | 1 | Một phần |
| B7 | C API 0.4 (`tc_analyzer_*`, `tc_set_tuning`, `tc_set_key_timeline`), engine theo timeline `[RT]`, tuning trong chỉnh giọng, ADR-003, test C | 0,5–1 | Không |
| B8 | *(nếu duyệt điểm 5)* Binding Dart 0.4, Phòng thử: tự dò tone khi nạp beat, thanh tiến độ, "Tone: La thứ (Am) – 68 BPM", sửa tay, hiện tone hiện tại thật to khi hát, cache kết quả theo file | mobile 0,5–1 | Không |
| B9 | Chạy bộ test 50 bài, `docs/key-results.md`, đo thời gian/RAM trên iPhone, báo cáo M3 | 0,5 | **Có** (test) |

Tổng ≈ **6,5–7 tuần DSP** (kế hoạch gốc 6 tuần) + 0,5–1 tuần mobile nếu làm B8.
B1–B3 và B7 làm được ngay. B4–B6 và B9 phải đợi dữ liệu.

**Cách bạn tự kiểm tra khi xong M3**:
- `ctest --preset host` xanh.
- `python3 tools/keytest/keytest.py --set test` → điểm trung bình ≥ 0,85, kèm báo cáo từng bài.
- `keylab analyze bai_bat_ky.mp3` để thử bài mới.
- Trên iPhone: nạp beat → thấy tone/BPM sau vài giây → hát có chỉnh giọng. Ở bài chuyển tone, key tự
  đổi đúng chỗ.

---

## 6. Rủi ro

| # | Rủi ro | Giảm thiểu |
|---|---|---|
| 1 | **Dữ liệu đến muộn**. B4–B6 và B9 đều chờ dữ liệu | Làm trước B1–B3, B7. Xin 20–30 bài dev sớm. Chọn tham số bằng GuitarSet + tổng hợp chỉ là tạm |
| 2 | **< 0,85** vì nhạc ngũ cung, dân ca và bolero mơ hồ trưởng/thứ tương đối (nhầm chỉ được 0,3) | Chroma bass, gợi ý chủ âm cuối bài, chọn profile trên dev. Nếu dev vẫn < 0,80 sau B4 → bàn với bạn phương án CNN nhỏ (ONNX Runtime, MIT). Phương án này cần dữ liệu huấn luyện có license và làm M3 dài thêm ~3–4 tuần |
| 3 | **Nhãn không nhất quán** (người gán coi intro là tone khác, hoặc gán tone chính thay vì tone vào bài) | Quy ước gán nhãn ở điểm 2. Bài điểm thấp được liệt kê để bạn nghe lại nhãn trước khi kết luận |
| 4 | Beat karaoke **lệch tuning** (bị dịch tone bằng phần mềm rẻ) hoặc có đoạn intro/outro tone khác | Ước lượng tuning, timeline, gộp đoạn ngắn |
| 5 | **Học vẹt bộ test** | Chỉ quét tham số trên dev. Script chặn quét trên test và ghi log mỗi lần chạy test |
| 6 | BPM nhầm bội số (bolero ~65 bị đoán ~130) | Prior theo thể loại. Engine trả kèm phương án ×2 / ×½ để UI cho người dùng chọn |
