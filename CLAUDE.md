# ChuẩnTone – Prompt xây dựng app + engine âm thanh ToneCore

> File này là spec gốc của dự án. AI lập trình (Claude Code, Cursor…) luôn đọc lại file này.
> Các quyết định đã chốt sau khi duyệt kế hoạch nằm ở mục 11 cuối file và trong `docs/adr/`.

---

## 0. VAI TRÒ & NGUYÊN TẮC

Bạn là trưởng nhóm kỹ thuật gồm: kỹ sư DSP/audio thời gian thực (C++), kỹ sư mobile (Flutter + native iOS/Android), kỹ sư backend/ML. Nhiệm vụ: xây dựng **ChuẩnTone** – app hát karaoke tự dò tone, tự chỉnh giọng cho người Việt – cùng với **ToneCore**, một audio SDK do chúng ta tự viết, đạt chất lượng tương đương các SDK thương mại (Superpowered, Switchboard, zplane, Agora audio effects).

Nguyên tắc bắt buộc:
1. **Clean-room**: tự thiết kế và tự viết từ tài liệu công khai (bài báo khoa học, sách DSP, tài liệu API hệ điều hành). KHÔNG sao chép mã, KHÔNG dịch ngược (reverse engineer) binary hay SDK của công ty khác, KHÔNG dùng plugin crack.
2. **License**: chỉ dùng thư viện MIT / BSD / Apache-2.0 / Zlib / ISC. Thư viện LGPL/GPL hoặc model AI có license hạn chế → DỪNG và hỏi tôi trước.
3. **Thương hiệu**: không dùng chữ "Auto-Tune"/"AutoTune" (thương hiệu của Antares) trong tên tính năng, UI hay marketing. Dùng "Tự chỉnh giọng", "Chỉnh tone tự động".
4. **Real-time safe**: trong audio callback không cấp phát bộ nhớ, không mutex, không I/O, không log, không gọi hàm có thể block.
5. **Làm theo milestone**, mỗi milestone chạy được, đo được, demo được. Lập kế hoạch và chờ tôi duyệt trước khi code.
6. Comment tiếng Việt cho các thuật toán DSP quan trọng. Khi chưa rõ yêu cầu → hỏi, không tự đoán.

---

## 1. SẢN PHẨM

- **Tên app**: ChuẩnTone
- **Khẩu hiệu**: "Bấm bài là hát"
- **Giá trị cốt lõi**: chọn bài → app tự dò tone → tự cài chỉnh giọng đúng tone → tự chọn hiệu ứng theo thể loại → hát hay ngay, không cần máy tính, soundcard hay kiến thức âm thanh.
- **Người dùng**: người hát nghiệp dư 18–50 tuổi, idol livestream TikTok/Facebook, người thích bolero, nhạc trẻ, ballad. Không rành kỹ thuật → UI tiếng Việt, tối giản, nút to, ít thuật ngữ.
- **Nền tảng**: Android (ưu tiên, Android 9+), iOS 15+.

---

## 2. KIẾN TRÚC TỔNG THỂ (monorepo)

```
chuantone/
├── engine/            # ToneCore SDK – C++17, không phụ thuộc nền tảng
│   ├── include/tonecore/   # C API công khai (tonecore.h)
│   ├── src/io/             # Audio I/O: android (Oboe), ios (RemoteIO)
│   ├── src/graph/          # Đồ thị xử lý, mixer, ring buffer lock-free
│   ├── src/pitch/          # Pitch detection, pitch correction, pitch shift
│   ├── src/key/            # Key detection, BPM
│   ├── src/fx/             # EQ, compressor, gate, de-esser, delay, reverb, limiter
│   ├── src/record/         # Thu âm nhiều track, bù độ trễ
│   ├── tests/              # GoogleTest + golden audio tests
│   └── bench/              # Benchmark CPU từng module
├── bindings/          # Dart FFI bọc C API
├── app/               # Flutter app ChuẩnTone
├── server/            # Python FastAPI: tách vocal, kho tone bài hát
├── tools/             # Đo độ trễ, chấm điểm dò tone (MIREX), tạo báo cáo
└── docs/              # Thiết kế, quyết định kỹ thuật (ADR), hướng dẫn build
```

---

## 3. ĐẶC TẢ ToneCore SDK

### 3.1 Audio I/O
- **Android**: Oboe (Apache-2.0), AAudio `PerformanceMode::LowLatency` + `SharingMode::Exclusive`, fallback OpenSL ES. Mono input, stereo output, 48 kHz, buffer = 2 × burst size. Hỗ trợ soundcard/micro USB-C (OTG).
- **iOS**: AVAudioSession `.playAndRecord` + mode `.measurement`, AudioUnit RemoteIO, IO buffer 128–256 frames.
- Phát hiện đổi route (cắm/rút tai nghe, Bluetooth, USB). Tai nghe Bluetooth → tắt nghe lại giọng (monitoring) và cảnh báo.
- **Đo độ trễ thực tế** bằng loopback test (phát xung → thu lại → tính offset). Lưu theo từng model máy, dùng để căn chỉnh khi thu âm.

### 3.2 Graph & mixer
- Các node xử lý cấp phát trước, kết nối qua đồ thị cố định; đổi preset bằng hot-swap không gây click (crossfade 10–20 ms).
- Tham số đổi từ UI qua atomic / lock-free queue, có làm mượt (parameter smoothing).
- Ring buffer SPSC lock-free cho dữ liệu giữa luồng audio và luồng ghi file/phân tích.

Chuỗi xử lý giọng mặc định:
`Mic → HPF 80Hz → Noise Gate → Pitch Detect → Pitch Correct → EQ → Compressor → De-esser → Delay → Reverb → Mixer(+Beat) → Limiter → Output / Recorder`

### 3.3 Pitch detection (dò cao độ giọng)
- Thuật toán: YIN hoặc McLeod Pitch Method (MPM), phân tích trên tín hiệu downsample 16 kHz để tiết kiệm CPU.
- Cửa sổ ~40 ms, hop 2.5–5 ms. Có voicing detection (không xử lý tiếng gió, phụ âm, khoảng lặng).
- Chống nhảy quãng 8 (octave error) bằng median filter + kiểm tra liên tục.
- Đầu ra: f0 (Hz), độ tin cậy 0–1, cờ voiced/unvoiced.

### 3.4 Pitch correction (chỉnh giọng)
- Lượng tử hóa cao độ về nốt gần nhất trong scale: trưởng, thứ tự nhiên, thứ hòa âm, ngũ cung, chromatic.
- Tham số:
  - `retuneSpeed` (0–200 ms)
  - `humanize`: giữ tự nhiên ở nốt ngân dài
  - `flexAmount`: chỉ chỉnh khi lệch quá ngưỡng
  - `vibratoPreserve`
  - `hysteresis`: tránh nhảy qua lại giữa 2 nốt
- **Pitch shifter**: TD-PSOLA đánh dấu chu kỳ theo f0 (tự giữ formant → không bị giọng chipmunk), crossfade mượt; đoạn unvoiced cho đi thẳng.
- 3 preset người dùng thấy:
  - **Tự nhiên**: ~40 ms, humanize cao
  - **Ca sĩ**: ~15 ms
  - **Robot**: 0 ms, không humanize
- Độ trễ thuật toán của khối pitch ≤ 15 ms.

### 3.5 Đổi tone beat (polyphonic pitch shift)
- Tăng/hạ ±6 nửa cung, giữ nguyên tempo.
- Phase vocoder có phase locking + giữ transient (trống không bị nhòe). Hoặc WSOLA + resample nếu nhẹ hơn mà chất lượng đạt.
- Chạy streaming cho file beat; cho phép render trước cả bài ở luồng nền để tiết kiệm pin.
- Khi đổi tone beat → engine tự cập nhật key của pitch correction.

### 3.6 Key detection (dò tone bài hát)
- Chạy offline trên beat (hoặc beat đã tách vocal).
- Chroma từ Constant-Q Transform, lọc thành phần trống (HPSS), so khớp với key profile (Krumhansl-Kessler, Temperley, EDMA) → chọn profile chính xác nhất trên bộ test.
- **Theo dõi chuyển tone** giữa bài bằng HMM/Viterbi trên từng đoạn ~4–8 giây. Đầu ra là timeline: `[{start, end, key, mode, confidence}]`.
- Kèm dò BPM (onset + autocorrelation) và đo loudness (LUFS) để chuẩn hóa âm lượng beat.
- Giai đoạn sau: thử model CNN nhỏ (ONNX) nếu thuật toán cổ điển < 85% trên bộ test.

### 3.7 Hiệu ứng (FX)
- **Noise gate**: attack, release, hold, threshold.
- **EQ**: 5 band biquad parametric + HPF/LPF.
- **Compressor**: RMS, knee, makeup gain.
- **De-esser**: compressor dải 5–9 kHz.
- **Delay/Echo**: đồng bộ theo BPM, feedback, filter.
- **Reverb**: Feedback Delay Network 8 line, các kiểu Room / Hall / Plate.
- **Limiter** đầu ra chống vỡ tiếng.
- *(Sau MVP)* Harmonizer/bè: dùng lại pitch shifter, bè quãng 3/quãng 5 theo scale.

### 3.8 Thu âm
- Ghi đồng thời: giọng khô (dry), giọng đã xử lý (wet), beat. WAV 48 kHz.
- Bù độ trễ đo được ở 3.1 để giọng khớp nhạc.
- Cho phép "render lại": áp preset khác lên giọng khô sau khi thu.
- Encode AAC/M4A bằng encoder của hệ điều hành (MediaCodec / AVAudioConverter).

### 3.9 C API (ổn định, dùng cho FFI)
Ví dụ các hàm:
- `tc_engine_create`, `tc_engine_start`, `tc_engine_stop`, `tc_engine_destroy`
- `tc_load_beat`, `tc_play`, `tc_pause`, `tc_seek`
- `tc_set_param(node_id, param_id, value)`, `tc_apply_preset(json)`
- `tc_set_key(root, scale)`, `tc_set_transpose(semitones)`
- `tc_analyze_file(path, callback)`: trả về key timeline, BPM, LUFS
- `tc_record_start(dir)`, `tc_record_stop`
- `tc_measure_latency`, `tc_get_metrics` (CPU, xruns, latency)

Callback trả về luồng UI qua queue, không gọi ngược từ audio thread.

### 3.10 Kiểm thử & đo lường
- **Unit test**: pitch detection trên sóng sin và giọng mẫu (sai số ≤ 5 cent), pitch shift (đo f0 đầu ra), filter (đáp ứng tần số).
- **Golden audio test**: xử lý file mẫu → so với file chuẩn (sai lệch dưới ngưỡng).
- **Benchmark CPU** từng node trên máy thật; báo cáo theo dạng bảng.
- `tools/keytest`: đọc bảng bộ test 50 bài (CSV: file, key, mode, modulation) → chấm điểm MIREX:
  - Đúng hoàn toàn: 1
  - Lệch quãng 5: 0,5
  - Nhầm tương đối trưởng/thứ: 0,3
  - Nhầm song song: 0,2
  - Sai: 0
  - Xuất báo cáo từng bài + điểm trung bình.
- `tools/latency`: đo round-trip latency, xuất kết quả theo model máy.

---

## 4. TÁCH VOCAL

- **Giai đoạn 1 (server)**: FastAPI + Demucs (htdemucs, MIT – kiểm tra lại license của trọng số model trước khi dùng thương mại). Hàng đợi job, giới hạn 10 phút/bài, tự xóa file sau 24 giờ, chỉ upload khi người dùng đồng ý.
- **Giai đoạn 2 (on-device)**: model nhỏ đã lượng tử hóa (ONNX Runtime / CoreML), chạy nền, có thanh tiến độ.
- Thanh trượt "Giữ giọng ca sĩ gốc" 0–100% để người mới hát theo.

---

## 5. TÍNH NĂNG APP (MVP)

### Màn hình
1. **Trang chủ**: bài gần đây, nút to "Thêm bài hát", bài mẫu.
2. **Phân tích bài**: thanh tiến độ (tách vocal → dò tone → dò BPM), hiển thị kết quả "Tone: La thứ (Am) – 92 BPM". Cho sửa tone tay nếu sai.
3. **Màn hát**:
   - Hiển thị tone hiện tại thật to.
   - 3 nút mức chỉnh giọng (Tự nhiên / Ca sĩ / Robot).
   - Nút tone − / + (±6).
   - Chip preset thể loại; nút "Vang" nhanh.
   - Lời bài hát chạy chữ nếu có file .lrc.
   - Đồng hồ cao độ (người hát thấy mình đang lệch lên hay xuống).
   - Nút thu âm/quay video.
4. **Nghe lại & chỉnh**: đổi preset trên giọng khô, chỉnh to nhỏ giọng/nhạc, cắt đầu đuôi.
5. **Xuất**: video 9:16 (camera trước hoặc nền sóng nhạc + tên bài), file âm thanh; nút chia sẻ TikTok / Facebook / Zalo.
6. **Cài đặt**: trình hướng dẫn đo độ trễ, chọn micro/tai nghe, chế độ soundcard OTG, chất lượng xuất.

### Dữ liệu
- SQLite cục bộ lưu thư viện bài, kết quả phân tích, preset người dùng.
- **Fingerprint bài hát**: tự viết dạng đơn giản (băm đỉnh phổ). Không dùng Chromaprint (LGPL) trừ khi tôi đồng ý.
- **Kho tone dùng chung (giai đoạn sau)**: khi người dùng sửa tone, gửi (fingerprint, tone đã sửa) lên server để xét duyệt và làm giàu kho. Đây là tài sản dữ liệu chiến lược.

### Ngoài phạm vi MVP (kiến trúc sẵn)
Chấm điểm, song ca, livestream trong app, kho bài có bản quyền, tài khoản & thanh toán (freemium).

---

## 6. PRESET THỂ LOẠI (giá trị khởi đầu, sẽ tinh chỉnh bằng tai)

| Preset | Chỉnh giọng | EQ | Compressor | Delay | Reverb |
|---|---|---|---|---|---|
| Bolero / Trữ tình | Tự nhiên 45 ms | +2 dB @ 3 kHz, −2 dB @ 300 Hz | 3:1 | 1/4 nốt, 25% | Hall 2.2 s, 22% |
| Ballad | Tự nhiên 35 ms | +1.5 dB @ 4 kHz | 3:1 | 1/8 nốt, 15% | Plate 1.8 s, 20% |
| Nhạc trẻ | Ca sĩ 15 ms | +3 dB @ 5 kHz, air +2 dB @ 12 kHz | 4:1 | 1/8 nốt, 18% | Plate 1.4 s, 15% |
| Remix | Ca sĩ 8 ms | +3 dB @ 5 kHz | 5:1 | 1/8 chấm, 22% | Room 1.0 s, 12% |
| Rap | Robot 0 ms (tùy chọn) | +2 dB @ 2.5 kHz | 6:1 | tắt | Room 0.6 s, 8% |
| Lofi | Tự nhiên 30 ms | LPF 9 kHz | 3:1 | 1/4 nốt, 20% | Room 1.2 s, 18% |

App tự gợi ý preset từ tên bài và BPM; người dùng đổi được.

---

## 7. YÊU CẦU PHI CHỨC NĂNG

- **Độ trễ** round-trip khi nghe lại giọng (tai nghe có dây): iOS ≤ 15 ms; Android flagship ≤ 20 ms; Android tầm trung (vd Samsung A35) ≤ 30 ms. *(Xem mục 11 – chỉ tiêu này áp dụng khi tắt chỉnh giọng.)*
- **CPU** chuỗi xử lý giọng ≤ 25% một nhân trên máy tầm trung. Không xrun/rè trong 30 phút hát liên tục.
- **Dò tone**: điểm MIREX trung bình ≥ 0,85 trên bộ test 50 bài Việt.
- **Ổn định**: crash-free ≥ 99,5%.
- **Kích thước app**: ≤ 80 MB (chưa gồm model on-device).
- **Offline**: hát, chỉnh giọng, thu âm hoạt động không cần mạng (trừ tách vocal giai đoạn 1).
- **Quyền riêng tư**: không upload âm thanh khi chưa được người dùng đồng ý.

---

## 8. PHÁP LÝ (bắt buộc tuân thủ)

- Clean-room như mục 0. Ghi lại nguồn tham khảo (bài báo, sách) cho từng thuật toán trong `docs/references.md`.
- Không đóng gói bài hát, beat hay lời có bản quyền vào app. Bài mẫu chỉ dùng nhạc royalty-free hoặc nhạc tôi cung cấp kèm giấy phép.
- Không có tính năng tải nhạc từ YouTube.
- Tạo `docs/licenses.md` liệt kê mọi thư viện bên thứ ba và license của chúng.
- Trước khi phát hành công khai, tạo `docs/patent-review.md` liệt kê các bằng sáng chế liên quan cần luật sư rà (vd: US10930256B2 về chỉnh cao độ real-time trong karaoke trên di động) và mô tả cách triển khai của chúng ta khác ở đâu.

---

## 9. MILESTONE

| # | Nội dung | Điều kiện hoàn thành |
|---|---|---|
| M0 | Khởi tạo monorepo, CI build engine cho Android/iOS, test chạy tự động | CI xanh, app Flutter rỗng gọi được `tc_engine_create` |
| M1 | Audio I/O + phát beat + thu mic + nghe lại giọng + công cụ đo độ trễ | Báo cáo độ trễ thực tế trên ≥ 3 máy |
| M2 | Pitch detection + chỉnh giọng real-time (tone chọn tay) | Unit test pass; demo 3 mức chỉnh; CPU đạt mục 7 |
| M3 | Dò tone + BPM + theo dõi chuyển tone; công cụ `keytest` | Điểm MIREX ≥ 0,85 trên bộ test |
| M4 | Đổi tone beat ±6, tự cập nhật key chỉnh giọng | Không méo/nhòe trống khi nghe A/B |
| M5 | Bộ FX đầy đủ + 6 preset thể loại + hot-swap không click | Demo đổi preset khi đang hát |
| M6 | Tách vocal (server) + màn phân tích bài | Bài 4 phút xử lý ≤ 60 giây |
| M7 | Thu âm đa track, render lại, xuất video 9:16, chia sẻ | Video xuất khớp tiếng, đăng được lên TikTok |
| M8 | UI tiếng Việt hoàn chỉnh, hướng dẫn lần đầu, cài đặt, OTG | Test với 5 người dùng thật không cần hướng dẫn |
| M9 | Tối ưu hiệu năng, test trên 10 máy Android phổ biến ở VN + 3 iPhone, chuẩn bị phát hành | Đạt toàn bộ mục 7 |

Cuối mỗi milestone báo cáo: đã xong gì, số đo (độ trễ, CPU, điểm dò tone), vấn đề còn lại, cách tôi tự kiểm tra.

---

## 10. BẮT ĐẦU

Kế hoạch kỹ thuật (cây thư mục, thư viện, thuật toán, rủi ro, ước lượng) đã được duyệt — xem `docs/plan.md`.

---

## 11. QUYẾT ĐỊNH ĐÃ CHỐT (sau khi duyệt kế hoạch)

- **Chỉ tiêu độ trễ** (ADR-002): mục 7 áp dụng khi tắt chỉnh giọng. Khi bật chỉnh giọng: iOS ≤ 25 ms, Android flagship ≤ 30 ms, tầm trung ≤ 40 ms. Khối pitch có độ trễ cố định 10 ms. Chỉ tiêu Android tầm trung chốt lại sau khi đo thật ở M1.
- **C API & FFI** (ADR-001): handle mờ, mã lỗi `tc_result`, sự kiện theo kiểu kéo (`tc_poll_events`), không gọi ngược từ audio thread.
- **Không dùng**: ffmpeg / ffmpeg_kit / libsndfile (LGPL), Rubber Band, SoundTouch, aubio, Essentia, libKeyFinder, Chromaprint. Xuất video bằng API native (AVAssetWriter / MediaCodec+MediaMuxer).
- **Thứ tự nền tảng** (2026-10-21): làm và nghiệm thu trên **iPhone trước**; Android (đo độ trễ ≥ 2 máy, chốt chỉ tiêu tầm trung) làm sau. Code Android vẫn giữ build được trong CI.
- **Upload tách vocal**: máy người dùng giải mã bằng codec OS rồi nén FLAC (libFLAC, BSD-3) — chốt lại ở M6.
- **M3** (2026-10-22, `docs/m3-plan.md`): bài chuyển tone chấm theo thời lượng trên timeline; lệch quãng 5 chỉ tính chiều lên (chuẩn MIREX/mir_eval); BPM Acc2 ≥ 90%, Acc1 ≥ 75%; có demo iPhone ở Phòng thử; phân tích là handle riêng `tc_analyzer_*` kiểu kéo (ADR-003 viết ở B7). Chỉ chỉnh tham số trên tập dev.

## 12. QUY ƯỚC CODE

- Engine: C++17, `namespace tonecore`, file `snake_case`, class `PascalCase`, hàm/biến `camelCase`, thành viên có hậu tố `_`. Định dạng bằng `.clang-format`.
- C API: tiền tố `tc_`, chỉ kiểu C thuần, không để exception vượt qua ranh giới C.
- Code chạy trong audio callback đánh dấu comment `// [RT]` và phải tuân thủ nguyên tắc 4.
- Mọi thư viện thêm vào → cập nhật `docs/licenses.md` trong cùng commit.
- Mọi thuật toán DSP mới → thêm nguồn vào `docs/references.md`.
- Build & test engine: `cmake --preset host && cmake --build --preset host && ctest --preset host`.
