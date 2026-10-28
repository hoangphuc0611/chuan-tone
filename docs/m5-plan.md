# M5 — Bộ FX đầy đủ + 6 preset thể loại + đổi preset không click (kế hoạch, chờ duyệt)

> Soạn 2026-10-28. Dựa trên CLAUDE.md mục 3.2, 3.7, 6, 7, 9; `docs/plan.md` mục 3.2, 3.7, rủi ro 4;
> ADR-002 (độ trễ).
> Điều kiện hoàn thành M5 (spec): **demo đổi preset khi đang hát** (không click, không hụt tiếng).

---

## Tiến độ

_(chưa bắt đầu)_

---

## Các điểm cần quyết

1. **Cách đổi preset: hai chuỗi FX chạy song song, đuôi vang của preset cũ tự tắt dần.**
   Preset mới được dựng ở luồng điều khiển (có cấp phát), rồi đẩy sang audio thread qua hàng đợi
   SPSC. Từ block kế tiếp, chuỗi mới nhận giọng:
   - **Phần "khô" đã xử lý** (EQ, compressor, de-esser) crossfade 15 ms từ chuỗi cũ sang chuỗi mới.
   - **Delay và reverb của chuỗi cũ** không nhận giọng mới nữa (đầu vào giảm về 0 trong 15 ms) nhưng
     **vẫn vang nốt** cho tới khi đuôi nhỏ hơn −80 dB, tối đa 4 s. Sau đó chuỗi cũ được trả về luồng
     nền để giải phóng.
   - Lý do: crossfade toàn chuỗi trong 15 ms sẽ **cắt cụt đuôi reverb** (nghe như "hụt" khi đổi từ
     Hall sang Room giữa câu hát). Ca sĩ thật đổi hiệu ứng thì đuôi cũ vẫn ngân. Đổi lại, trong lúc
     đuôi cũ còn vang (≤ 4 s) thì CPU của delay và reverb tăng gấp đôi.
   - Đổi liên tục nhiều lần: giữ tối đa 2 chuỗi cũ; chuỗi cũ nhất bị tắt dần trong 15 ms.
   - Phương án khác: chỉ một chuỗi, thông số trượt dần sang preset mới. Đổi độ dài dòng trễ (thời
     gian delay, kích thước phòng) khi đang chạy sẽ làm cao độ đuôi vang trượt (hiệu ứng Doppler),
     nên không dùng cho đổi preset.
   - **Đề nghị**: hai chuỗi + đuôi tự tắt.
2. **Đổi một thông số lẻ** (thanh "Vang", hoặc chỉnh tay khi tinh chỉnh bằng tai). Thông số liên tục
   (độ lớn, ngưỡng, tần số EQ, độ "ướt") được làm mượt ~20 ms ngay trên chuỗi đang chạy, không dựng
   chuỗi mới. Thông số đổi độ dài dòng trễ (thời gian delay, kích thước phòng, pre-delay) thì engine
   tự đi đường đổi preset ở điểm 1. **Đề nghị**: như trên.
3. **Limiter đầu ra** (thay chỗ kẹp ±1 hiện tại).
   - Nhìn trước 1 ms, trần −0,2 dBFS, hai kênh dùng chung một hệ số, dưới trần thì đi thẳng. Dùng
     lại `PeakLimiter` của M4 (đã có test) sau khi kiểm RT-safe.
   - Giọng nghe lại trễ thêm **1 ms**: iPhone loa ngoài 12,6 → ~13,6 ms, vẫn ≤ 15 ms (ADR-002).
   - Đo đỉnh theo mẫu; true-peak (lấy mẫu ×4) để dành cho xuất file ở M7.
   - Phương án khác: không nhìn trước, kẹp mềm. Không thêm trễ nhưng méo khi giọng to + beat to.
   - **Đề nghị**: nhìn trước 1 ms.
4. **Cách truyền preset qua C API.**
   - Dùng **struct C `tc_fx_preset`** (có `struct_size` như các struct khác), thay cho
     `tc_apply_preset(json)` trong ví dụ của spec.
   - Lý do: (a) không thêm thư viện JSON vào engine; (b) ffigen sinh sẵn kiểu cho Dart, sai tên
     trường là lỗi biên dịch; (c) app vẫn lưu preset người dùng dạng JSON ở phía Dart. Khi cần
     nhận preset từ server (giai đoạn sau) thì Dart đọc JSON rồi điền struct.
   - **Đề nghị**: struct.
5. **Preset thể loại và chỉnh giọng.**
   - Chọn preset thì đặt luôn mức chỉnh giọng theo cột "Chỉnh giọng" của bảng mục 6. Riêng Rap mặc
     định **tắt** chỉnh giọng; Robot vẫn bấm được.
   - Sau đó người dùng bấm nút chỉnh giọng nào thì giữ lựa chọn đó, cho tới khi chọn preset khác.
   - Thêm preset thứ 7 **"Khô"** (không hiệu ứng) để so sánh và cho người thích giọng mộc.
   - Engine giữ FX và chỉnh giọng là hai API riêng; app ghép chúng theo bảng.
   - **Đề nghị**: như trên.
6. **Gợi ý preset theo bài** (mục 6: "App tự gợi ý preset từ tên bài và BPM").
   - Làm luôn ở M5 bằng luật đơn giản, có test:
     - tên file có "remix", "rap", "lofi", "bolero" thì chọn preset tương ứng;
     - còn lại theo BPM (≥ 118 → Nhạc trẻ, ≤ 80 → Ballad);
     - không đoán được thì giữ preset người dùng chọn lần trước.
   - Chỉ gợi ý (hiện chip đang chọn), người dùng đổi được. App nhớ preset theo bài như nhớ tone.
   - **Đề nghị**: có, làm ở M5.
7. **Cách nghe và nghiệm thu.**
   - **Nghe trên Mac:** dùng giọng hát khô có giấy phép (vocadito, CC BY 4.0, đã dùng ở M2) + beat
     dev trong `testdata/`. Trang nghe **theo preset** (không mù, vì đây là chỉnh "chất" chứ không
     so hai thuật toán) để bạn tinh chỉnh bằng tai. Mỗi preset có 3 đoạn: Khô / preset / preset +
     beat.
   - **Demo trên iPhone:** nghe lại giọng bằng **tai nghe có dây** (Bluetooth bị chặn nghe lại vì trễ
     ~200 ms), đổi preset khi đang hát.
   - **Đạt** khi: không nghe tiếng click / bụp / hụt tiếng lúc đổi; mỗi preset nghe "đúng chất" thể
     loại (bạn chấm); không méo khi hát to.
   - **Đề nghị**: như trên. Bạn cần chuẩn bị một tai nghe có dây (EarPods cổng Lightning hoặc USB-C).

---

## 1. Kết quả đầu ra

- **Engine** — chuỗi giọng đầy đủ theo mục 3.2:
  `Mic → HPF 80 Hz → Gate → [dò cao độ → chỉnh giọng] → EQ → Compressor → De-esser → Delay → Reverb
  → trộn với beat → Limiter → ra / (M7: thu)`.
  - HPF và gate đứng trước dò cao độ, thông số chung (không theo preset).
  - Phần từ EQ tới reverb là **chuỗi FX đổi theo preset**.
  - Giọng mono tới hết delay; reverb ra stereo.
- **C API 0.6:** preset FX, thông số lẻ có làm mượt, BPM cho delay (mục 4).
- **`fxlab` (CLI):** render file giọng (± beat) qua preset → WAV; đo các chỉ số (đáp ứng tần số,
  đường nén, RT60…).
- **App (Phòng thử):**
  - chip 7 preset;
  - thanh "Vang" nhanh;
  - preset gợi ý theo bài, nhớ theo bài;
  - dòng CPU tách riêng phần FX.
- **Tài liệu:**
  - `docs/fx-results.md` (số đo);
  - ADR-005 (chuỗi FX và cách đổi preset);
  - `docs/references.md` (nguồn từng thuật toán);
  - `docs/licenses.md` không thêm thư viện.

---

## 2. Thuật toán từng khối

| Khối | Thuật toán | Trễ | Nguồn |
|---|---|---|---|
| HPF 80 Hz | Biquad bậc 2 (đã có ở M2) | 0 | RBJ Audio EQ Cookbook |
| Gate | Bao biên đỉnh trên tín hiệu đã HPF; mở ở ngưỡng, đóng ở ngưỡng − 6 dB (chống "nháy"); attack 1 ms, hold 80 ms, release 150 ms; khi đóng chỉ giảm −30 dB chứ không tắt hẳn (giữ phụ âm cuối nhẹ của tiếng Việt) | 0 | Zölzer, *DAFX* ch. 4 |
| EQ | HPF/LPF + 5 băng (bell / low shelf / high shelf) bằng **SVF dạng TPT** — đáp ứng như RBJ nhưng ổn định khi thông số trượt (không "zipper", không nổ) | 0 | Zavalishin, *The Art of VA Filter Design*; Simper (SVF tuyến tính) |
| Compressor | Feed-forward; bao biên RMS ~10 ms trong miền dB; knee mềm; attack/release làm mượt trong miền log; makeup gain | 0 | Giannoulis, Massberg & Reiss, JAES 2012 |
| De-esser | Tách băng bù trừ: x_cao = SVF thông cao 5 kHz, x_thấp = x − x_cao (cộng lại đúng tuyệt đối). Bao biên băng 5–9 kHz điều khiển mức giảm, **chỉ áp lên x_cao**, tối đa −8 dB (không làm "ngọng" s/x/ch) | 0 | Zölzer, *DAFX*; Giannoulis 2012 |
| Delay | Dòng trễ phân số (nội suy Lagrange bậc 3); thời gian = 60/BPM × phần nốt (1/4, 1/8, 1/8 chấm); LPF + HPF trong vòng hồi tiếp (vọng sau tối dần); mono | 0 | Zölzer, *DAFX* ch. 2 |
| Reverb | **FDN 8 dòng**, ma trận Hadamard (trực giao); độ dài dòng là các số nguyên tố cùng nhau; lọc hấp thụ mỗi dòng theo RT60 thấp/cao (Jot); 4 bộ khuếch tán all-pass ở đầu vào (mật độ vọng cao, không "lộp độp"); pre-delay; 2 tổ hợp ra trực giao → stereo rộng. Room / Hall / Plate = bộ tham số | 0 | Jot & Chaigne 1991; Schroeder 1962; Smith, *Physical Audio Signal Processing* |
| Limiter | `PeakLimiter` (M4): nhìn trước 1 ms, release 60 ms, hai kênh chung hệ số | **1 ms** | — |

Tổng trễ nghe lại tăng thêm: **1 ms** (limiter). Mọi khối FX khác không thêm trễ.

---

## 3. Đồ thị, đổi preset, thông số

- **`FxChain`** = EQ → Compressor → De-esser → Delay → Reverb, cấp phát toàn bộ khi dựng. Engine giữ
  1 chuỗi đang dùng + tối đa 2 chuỗi cũ đang "vang nốt".
- **Đổi preset** (điểm 1):
  1. Luồng điều khiển dựng `FxChain` mới.
  2. Đẩy con trỏ qua SPSC.
  3. Audio thread crossfade phần khô 15 ms, ngắt đầu vào delay/reverb cũ, đo năng lượng đuôi cũ.
  4. Khi đuôi < −80 dB (hoặc quá 4 s), trả chuỗi cũ về luồng điều khiển qua SPSC để huỷ.
  - Cùng mẫu như `BeatPlayer` ở M4 (đã qua TSan).
- **Thông số lẻ** (`tc_set_param`): atomic + làm mượt one-pole ~20 ms. Thông số đổi độ dài dòng
  trễ được chuyển thành đổi preset (điểm 2).
- **RT-safe:** audio thread không cấp phát, không khoá, kể cả lúc đổi preset. Thêm ca vào
  `rt_alloc_test`; TSan.
- **CPU:** ghi riêng thời gian chuỗi FX vào `tc_metrics` (`fx_load`) để biết FX tốn bao nhiêu trên máy
  thật.

---

## 4. C API 0.6

```c
typedef enum tc_genre {
  TC_GENRE_DRY = 0, TC_GENRE_BOLERO = 1, TC_GENRE_BALLAD = 2, TC_GENRE_POP = 3 /* Nhạc trẻ */,
  TC_GENRE_REMIX = 4, TC_GENRE_RAP = 5, TC_GENRE_LOFI = 6
} tc_genre;

typedef struct tc_fx_preset {
  uint32_t struct_size;
  /* EQ: thông thấp / thông cao (0 = tắt) + 5 băng {loại, tần số, dB, Q} */
  /* Compressor: ngưỡng, tỉ lệ, knee, attack, release, makeup */
  /* De-esser: tần số, ngưỡng, mức giảm tối đa */
  /* Delay: phần nốt (tắt / 1/4 / 1/8 / 1/8 chấm / ms), ms, hồi tiếp, độ ướt, LPF, HPF */
  /* Reverb: kiểu (room / hall / plate), RT60, pre-delay, giảm cao, độ ướt */
} tc_fx_preset;

TC_EXPORT void tc_fx_preset_init(tc_fx_preset* preset, int32_t genre);  /* điền theo bảng mục 6 */
TC_EXPORT tc_result tc_apply_fx_preset(tc_engine* engine, const tc_fx_preset* preset);
TC_EXPORT tc_result tc_get_fx_preset(tc_engine* engine, tc_fx_preset* out_preset);
TC_EXPORT tc_result tc_set_tempo(tc_engine* engine, float bpm);  /* delay theo nhịp; 0 = 100 BPM */
```

- Node mới cho `tc_set_param`: `TC_NODE_GATE`, `TC_NODE_DELAY`, `TC_NODE_REVERB`, `TC_NODE_FX`.
- Tham số: `TC_PARAM_MIX` (độ ướt — thanh "Vang" chỉnh độ ướt reverb + delay theo tỉ lệ của preset),
  `TC_PARAM_THRESHOLD_DB` (gate), `TC_PARAM_ENABLED` (bỏ qua cả chuỗi FX).
- Binding Dart 0.6: `FxPreset` (đọc/ghi các trường, `toJson` / `fromJson` cho lưu trữ),
  `engine.applyFxPreset`, `engine.fxMix`, `engine.tempo`.

---

## 5. Preset khởi đầu (bảng mục 6 → thông số)

Giá trị khởi đầu, sẽ chỉnh bằng tai ở B6 và trên iPhone. Chung cho mọi preset:
- **HPF giọng:** 80 Hz.
- **Compressor:** knee 6 dB, attack 5 ms, release 80 ms, makeup tự tính để mức to ≈ như khô.
- **De-esser:** 6 kHz, giảm tối đa 6 dB.
- **"%" ở cột Delay / Reverb:** độ ướt so với tiếng khô (22% ≈ −13 dB).

| Preset | Chỉnh giọng | EQ | Comp | Delay | Reverb |
|---|---|---|---|---|---|
| Khô | (giữ nguyên) | — | — | tắt | tắt |
| Bolero / Trữ tình | Tự nhiên 45 ms | bell +2 dB @ 3 kHz Q 1; bell −2 dB @ 300 Hz Q 1 | −18 dB, 3:1 | 1/4 nốt, hồi tiếp 25%, ướt 25%, LPF 4 kHz | Hall RT60 2,2 s, pre-delay 30 ms, ướt 22% |
| Ballad | Tự nhiên 35 ms | bell +1,5 dB @ 4 kHz | −18 dB, 3:1 | 1/8, 20%, ướt 15% | Plate 1,8 s, 20 ms, ướt 20% |
| Nhạc trẻ | Ca sĩ 15 ms | bell +3 dB @ 5 kHz; high shelf +2 dB @ 12 kHz | −20 dB, 4:1 | 1/8, 20%, ướt 18% | Plate 1,4 s, 15 ms, ướt 15% |
| Remix | Ca sĩ 8 ms | bell +3 dB @ 5 kHz | −22 dB, 5:1 | 1/8 chấm, 30%, ướt 22% | Room 1,0 s, 10 ms, ướt 12% |
| Rap | tắt (Robot tuỳ chọn) | bell +2 dB @ 2,5 kHz | −24 dB, 6:1 | tắt | Room 0,6 s, 5 ms, ướt 8% |
| Lofi | Tự nhiên 30 ms | LPF 9 kHz; HPF 150 Hz | −18 dB, 3:1 | 1/4, 30%, ướt 20%, LPF 2,5 kHz | Room 1,2 s, 20 ms, ướt 18% |

---

## 6. Test và số đo

**Unit test** (CI, tín hiệu tổng hợp có đáp án):

| Khối | Phép thử | Chỉ tiêu đề xuất |
|---|---|---|
| EQ / HPF / LPF | Đáp ứng biên độ tại 48 tần số 20 Hz–20 kHz so với công thức RBJ | ±0,1 dB |
| EQ | Quét tần số / dB liên tục trong 2 s | không nổ (luôn hữu hạn); năng lượng cao tần thêm vào ≤ −60 dB so với tín hiệu (không zipper) |
| Compressor | Đường tĩnh vào → ra (−60…0 dB) | khớp công thức knee ±0,5 dB |
| Compressor | Bậc nhảy ±10 dB | hằng thời gian attack / release ±20% |
| Gate | Tín hiệu dao động quanh ngưỡng ±2 dB | không nháy (≤ 1 lần đổi trạng thái / hold); mở ≤ attack + 1 ms; khi đóng giảm đúng −30 dB |
| De-esser | Chuỗi "s" tổng hợp (nhiễu 5–9 kHz +12 dB) + nguyên âm (hoạ âm < 4 kHz) | "s" giảm ≥ 5 dB; nguyên âm đổi ≤ 0,5 dB; dưới ngưỡng thì x_thấp + x_cao = x (sai ≤ 1e-6) |
| Delay | Xung, 92 BPM, 1/8 chấm | vọng đầu đúng vị trí ±1 mẫu; mỗi vòng giảm đúng hồi tiếp ±0,5 dB |
| Reverb | Xung → RT60 (tích phân ngược Schroeder) ở 500 Hz–2 kHz | ±15% so với đặt; không DC; tương quan L–R của đuôi ≤ 0,3 |
| Reverb | Phổ đuôi 200–600 ms | không có cộng hưởng nhô > 12 dB trên trung bình (không "kim loại") |
| Limiter | Đầu vào +12 dB | không mẫu nào vượt trần; dưới trần đúng từng mẫu (sau trễ 1 ms) |
| Đổi preset | Giọng tổng hợp (M2) liên tục; đổi preset 20 lần ngẫu nhiên khi đang phát | không bước nhảy mẫu lớn hơn đoạn không đổi; năng lượng ngắn hạn không tụt > 3 dB lúc chuyển; đuôi reverb cũ tắt theo RT60 (không cụt trong 15 ms) |
| RT | Đổi preset, đặt thông số khi đang chạy | không cấp phát / khoá ở audio thread (`rt_alloc_test`); TSan sạch |

**Hiệu năng** (Mac M1 trong bench; iPhone qua Phòng thử):
- **CPU cả chuỗi giọng** (chỉnh giọng + FX): ≤ 25% một nhân trên máy tầm trung (mục 7). Trên iPhone
  ước lượng FX < 3%. Bench từng khối: Mac M1, và Linux trong CI.
- **CPU lúc đổi preset:** đỉnh (2 chuỗi vang cùng lúc) vẫn ≤ 25%.
- **Trễ nghe lại:** chỉ tăng 1 ms. Đo bằng màn "Đo độ trễ" trên iPhone: loa ngoài ≤ 15 ms (hiện
  12,6 ms).
- **Soak 30 phút trên host:** không xrun, không NaN, không denormal (đuôi reverb tắt dần; FTZ đã bật).

**Nghe:**
- `fxlab` render giọng vocadito (CC BY 4.0) ± beat dev → `testdata/fx-ab/` (không commit, không đưa
  lên CI).
- Demo iPhone với tai nghe có dây.

---

## 7. Các bước

| Bước | Nội dung | Tuần DSP |
|---|---|---|
| B1 | Thư mục `fx/`; tiện ích đo (đáp ứng tần số, RT60, đường nén); `fxlab`; HPF + gate; EQ (SVF) + test | 0,75 |
| B2 | Compressor + de-esser + test | 0,75 |
| B3 | Delay theo BPM + reverb FDN 8 dòng (Room / Hall / Plate) + test + bench | 1,25 |
| B4 | Limiter đầu ra (thay kẹp ±1) + trộn; đo trễ | 0,25 |
| B5 | Engine: `FxChain`, đổi preset hai chuỗi + đuôi tự tắt, thông số làm mượt; C API 0.6; RT / TSan; ADR-005 | 1 |
| B6 | 7 preset; `fxlab` + trang nghe theo preset; chỉnh bằng tai lần 1 (với bạn) | 0,5 |
| B7 | Binding Dart 0.6 + Phòng thử: chip preset, "Vang", gợi ý theo bài, nhớ theo bài | 0,5 (mobile) |
| B8 | Đo CPU / trễ trên iPhone, demo đổi preset khi đang hát, `docs/fx-results.md`, báo cáo M5 | 0,5 |

Tổng ≈ **5 tuần DSP** + 0,5 tuần mobile (kế hoạch gốc: 5 + 1).

**Cách bạn tự kiểm tra khi xong M5**:
- `ctest --preset host` xanh (gồm các test ở mục 6).
- `fxlab render giong.wav --preset bolero -o out.wav` rồi nghe; mở `testdata/fx-ab/index.html`.
- Trên iPhone, đeo tai nghe có dây, bật "Nghe lại giọng", hát và bấm qua các chip preset:
  - không có tiếng click, đuôi vang cũ ngân nốt;
  - thanh "Vang" chỉnh mượt;
  - dòng CPU ở Phòng thử vẫn thấp.

---

## 8. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | **Reverb nghe "kim loại", rỗng** hoặc lộp độp | All-pass khuếch tán đầu vào, độ dài dòng nguyên tố cùng nhau, lọc hấp thụ theo RT60, test phổ đuôi; nếu vẫn kim loại: điều chế nhẹ độ dài dòng (0,1–0,3 ms, 0,5 Hz) |
| 2 | **CPU máy yếu** (FDN 8 dòng, hai chuỗi lúc đổi preset) | Bench từng khối; đuôi cũ tối đa 4 s; mức chất lượng "reverb 4 dòng" cho máy yếu để dành M9 (rủi ro 4 trong `docs/plan.md`) |
| 3 | **Gate cắt phụ âm** đầu / cuối câu tiếng Việt (hơi nhẹ, dấu hỏi / ngã cuối câu) | Gate chỉ giảm −30 dB thay vì tắt hẳn, hold 80 ms, ngưỡng thấp; nghe trên câu hát Việt |
| 4 | **De-esser làm ngọng** s / x / ch / tr | Chỉ giảm băng cao, tối đa −6…−8 dB; test nguyên âm không đổi |
| 5 | **Rú (hú) khi dùng loa ngoài** + nghe lại giọng + reverb / delay hồi tiếp | App đã cảnh báo khi nghe lại qua loa ngoài; giới hạn hồi tiếp delay ≤ 50%; demo bằng tai nghe có dây |
| 6 | **Tổng to lên** khi thêm vang (giọng + đuôi + beat) → limiter làm việc liên tục, nghe "bóp" | Makeup và độ ướt được đặt sao cho mức to ≈ như khô (đo LUFS trong `fxlab`); limiter chỉ bắt đỉnh |
| 7 | Bluetooth: không nghe lại được giọng → không nghe được FX khi hát | Đúng như spec (Bluetooth trễ ~200 ms). FX vẫn áp vào bản thu ở M7. Demo M5 dùng tai nghe có dây |

Việc còn mở không chặn M5: nhãn BPM nhạc Việt (M3); nâng tone +4…+6 "hơi phơ" (M4 → M9); nghiệm thu
M2 trên iPhone (nghe lại giọng + chỉnh giọng bằng tai nghe có dây) — có thể làm cùng lúc với demo M5.
