# ChuẩnTone + ToneCore — Kế hoạch kỹ thuật (đã duyệt 2026-10-20)

## Bối cảnh
- Thư mục `/Users/macprom1/Documents/chuan-tone` đang **trống**, chưa phải git repo → dựng từ đầu.
- Máy dev hiện **chưa có**: CMake, Ninja, Flutter/Dart, Xcode đầy đủ (chỉ có Command Line Tools), Java/JDK, Android SDK/NDK. Python hệ thống 3.9.6 (cần ≥ 3.11 cho server/tools).
- Mục tiêu tài liệu này: trả lời 5 mục ở phần 10 của spec, nêu các điểm mâu thuẫn trong spec cần bạn quyết, và mô tả chính xác M0 sẽ làm gì khi được duyệt.

---

## ⚠️ Điểm cần bạn quyết trước (phát hiện khi đọc spec)

1. **Mâu thuẫn độ trễ**: mục 7 yêu cầu round-trip iOS ≤ 15 ms, nhưng mục 3.4 cho riêng khối pitch ≤ 15 ms. Phần cứng iOS (buffer 128 frame) đã tốn ~8–12 ms, cộng PSOLA (~10 ms) → ~20–25 ms khi BẬT chỉnh giọng.
   **Đề xuất**: chỉ tiêu mục 7 áp dụng khi *tắt chỉnh giọng* (chỉ EQ/comp/reverb); khi bật chỉnh giọng: iOS ≤ 25 ms, Android flagship ≤ 30 ms, tầm trung ≤ 40 ms. Khối pitch dùng độ trễ **cố định 10 ms** (đủ cho f0 ≥ 100 Hz; giọng nam trầm hơn dùng grain cắt ngắn).
2. **Android tầm trung ≤ 30 ms có thể không đạt** trên một số máy Samsung A-series (đường audio của hãng). Cần đo thật ở tuần 1–2 của M1 rồi mới chốt chỉ tiêu. Phương án dự phòng: chế độ "Soundcard OTG" dùng nghe lại bằng phần cứng của soundcard (đa số idol livestream VN đã dùng soundcard).
3. **Tách vocal trên server và LGPL**: giải mã MP3/M4A trên server thường cần ffmpeg hoặc libsndfile (đều LGPL). **Đề xuất**: máy người dùng tự giải mã bằng codec hệ điều hành rồi nén **FLAC (libFLAC, BSD-3)** trước khi upload → server không cần thư viện LGPL. Quyết ở M6.
4. **Xuất video**: `ffmpeg_kit` (LGPL/GPL, đã ngừng phát triển) → **không dùng**. Dùng AVAssetWriter (iOS) và MediaCodec + MediaMuxer (Android) qua kênh native.
5. **Không có kỹ sư backend** trong đội 2 người → kỹ sư mobile kiêm server M6 (đã tính vào ước lượng), hoặc thuê ngoài.
6. **Dữ liệu bạn cần cung cấp**: (a) bộ test 50 bài Việt có nhãn tone do người biết nhạc gán; (b) thêm ~150 bài *khác* để tinh chỉnh thuật toán, tránh "học vẹt" đúng 50 bài test; (c) 20–30 đoạn hát mộc (dry) người Việt nam/nữ cho test chỉnh giọng; (d) beat mẫu royalty-free.

---

## 1. Cây thư mục & công cụ build

```
chuantone/
├── CLAUDE.md                      # spec gốc (bản bạn dán) + quy ước làm việc
├── .github/workflows/             # engine.yml, android.yml, ios.yml, server.yml
├── .clang-format  .clang-tidy  .editorconfig
├── engine/                        # ToneCore — C++17
│   ├── CMakeLists.txt  CMakePresets.json   # host-macos, host-linux, android-arm64, android-x64, ios
│   ├── cmake/                     # toolchain, warnings, sanitizers, xcframework.cmake
│   ├── include/tonecore/tonecore.h        # C API duy nhất được export
│   ├── src/
│   │   ├── api/                   # tonecore.cpp: lớp C → C++, bắt exception, mã lỗi
│   │   ├── core/                  # spsc_ring.h, smoothed_param.h, denormal.h, rt_assert.h, simd.h
│   │   ├── io/  android/ (oboe_backend.cpp, latency_probe.cpp)  ios/ (remoteio_backend.mm, session.mm)  null/ (offline backend cho test)
│   │   ├── graph/                 # node.h, graph.cpp, mixer.cpp, preset_swap.cpp, command_queue.h
│   │   ├── dsp/                   # fft (bọc PFFFT/vDSP), biquad, svf, resampler, delay_line, window
│   │   ├── pitch/                 # mpm.cpp, yin.cpp, f0_tracker.cpp, corrector.cpp, psola.cpp, scales.cpp
│   │   ├── stretch/               # phase_vocoder.cpp, transient.cpp, beat_transposer.cpp
│   │   ├── key/                   # cqt.cpp, hpss.cpp, chroma.cpp, tuning.cpp, key_profiles.cpp, key_hmm.cpp, tempo.cpp, loudness.cpp
│   │   ├── fx/                    # gate, eq5, compressor, deesser, delay, fdn_reverb, limiter
│   │   ├── record/                # wav_writer, recorder, latency_comp, encoder_android, encoder_ios
│   │   ├── decode/                # decoder_android (AMediaExtractor), decoder_ios (ExtAudioFile)
│   │   └── fingerprint/           # băm đỉnh phổ
│   ├── third_party/               # mã nguồn vendored, ghim version + file LICENSE
│   ├── tests/  unit/  golden/ (input/, expected/, tolerances.json)  data/
│   ├── bench/                     # Google Benchmark, mỗi node một bench
│   └── cli/                       # tc_cli: render offline file → file (dùng cho golden test + keytest)
├── bindings/tonecore_flutter/     # Flutter FFI plugin (template plugin_ffi)
│   ├── lib/  src/generated_bindings.dart (ffigen)  tonecore.dart (API Dart thân thiện)
│   ├── android/  (build.gradle → externalNativeBuild CMake trỏ về ../../engine)
│   ├── ios/      (podspec dùng ToneCore.xcframework)
│   └── ffigen.yaml
├── app/                           # Flutter app ChuẩnTone
│   ├── lib/ app.dart  features/{home,analyze,sing,review,export,settings}/  data/ (drift/SQLite)  l10n/ (vi)
│   ├── android/  ios/  (code native: audio session, video export, share)
│   └── test/  integration_test/
├── server/                        # FastAPI
│   ├── pyproject.toml (uv)  Dockerfile
│   ├── app/ main.py  jobs.py  separation.py  storage.py (TTL 24h)
│   └── tests/
├── tools/
│   ├── keytest/   (C++ dùng engine qua tc_cli + script Python xuất báo cáo; kiểm chéo điểm với mir_eval)
│   ├── latency/   (gom kết quả đo theo model máy → CSV/Markdown)
│   ├── benchreport/
│   └── datasets/  (script kiểm tra CSV bộ test, KHÔNG commit audio có bản quyền)
└── docs/  adr/  references.md  licenses.md  patent-review.md  build.md  latency-results.md
```

**Công cụ build**
| Phần | Công cụ |
|---|---|
| Engine | CMake ≥ 3.28 + Ninja, `CMakePresets.json`; clang; ASan/UBSan/TSan cho bản host |
| Android | Android Studio, Gradle/AGP 8.x, **NDK r27 (LTS)**, minSdk 28 (Android 9), ABI: arm64-v8a, x86_64 (emulator), armeabi-v7a tùy chọn |
| iOS | Xcode 16+, CMake `-G Xcode` → **ToneCore.xcframework** (device + simulator), CocoaPods trong plugin; iOS 15+ |
| Flutter | Flutter stable mới nhất lúc làm M0, ghim bằng **FVM**; `ffigen` sinh binding từ `tonecore.h` |
| Server/tools | Python 3.12, **uv**, Docker |
| CI | GitHub Actions: (1) Linux: build + test engine + sanitizer; (2) Ubuntu: build APK; (3) macOS: xcframework + `flutter build ios --no-codesign`; (4) server: pytest |

**Cần cài trên máy dev trước M0**: Xcode (App Store), Android Studio + NDK r27 + JDK 17, `brew install cmake ninja fvm uv`.

---

## 2. Thư viện bên thứ ba (dự kiến)

| Thư viện | Dùng cho | License | Ghi chú |
|---|---|---|---|
| Oboe | Audio I/O Android | Apache-2.0 | |
| PFFFT | FFT (SIMD NEON/SSE) | BSD-like (FFTPACK) | iOS có thể dùng vDSP (hệ điều hành) |
| dr_wav / dr_mp3 / dr_flac | Đọc file cho test/CLI | Public domain / MIT-0 | App dùng codec của OS |
| libFLAC | Nén FLAC trước khi upload (M6) | BSD-3 | chỉ nếu chọn phương án ở điểm 3 |
| nlohmann/json | Đọc preset JSON (ngoài audio thread) | MIT | |
| GoogleTest / Google Benchmark | Test / bench | BSD-3 / Apache-2.0 | |
| ONNX Runtime | Model on-device (giai đoạn sau) | MIT | |
| Flutter: ffi, ffigen, path_provider, camera, share_plus, go_router | | BSD-3 | camera: tắt thu tiếng của plugin |
| Flutter: riverpod, drift (SQLite), permission_handler | | MIT | |
| Server: FastAPI, uvicorn, PyTorch, Demucs (code) | | MIT / BSD-3 / BSD-3 / MIT | **trọng số htdemucs: cần xác minh license riêng trước khi thương mại** |
| Server queue: arq + Valkey | Hàng đợi job | MIT / BSD-3 | không dùng Redis ≥ 7.4 (đổi license) |
| Tools: numpy, mir_eval, matplotlib | Báo cáo, kiểm chéo | BSD / MIT / PSF-BSD | chỉ dùng trong tools, không ship |
| *(tham chiếu A/B)* Signalsmith Stretch | So chất lượng đổi tone beat | MIT | chỉ để so sánh; bản ship tự viết, trừ khi bạn muốn dùng thẳng |

**Đã loại vì license**: Rubber Band (GPL), SoundTouch (LGPL), aubio (GPL), Essentia (AGPL), libKeyFinder (GPL), Chromaprint (LGPL), ffmpeg/ffmpeg_kit (LGPL/GPL), libsndfile (LGPL), madmom models (CC BY-NC-SA).

---

## 3. Thuật toán cho từng module

**3.1 Audio I/O**
- Android: Oboe, AAudio LowLatency + Exclusive, **input preset `VoicePerformance`** (Android 10+; tắt AGC/khử ồn, giúp đi đường nhanh), 48 kHz đúng tần số native (tránh resampler của hệ thống), full-duplex: output callback đọc non-blocking từ input stream. Tự tăng buffer khi có xrun (kiểu LatencyTuner), dùng **ADPF Performance Hint** (Android 12+) để luồng audio không bị đẩy sang nhân yếu.
- iOS: `.playAndRecord` + `.measurement` + `.defaultToSpeaker/.allowBluetoothA2DP`, IOBufferDuration 128–256 frame, RemoteIO.
- Đo độ trễ: phát chuỗi **MLS** (hoặc chirp), thu lại, **tương quan chéo** → offset theo mẫu; lặp 5 lần lấy trung vị; kiểm tra chéo với timestamp AAudio/`AVAudioSession.inputLatency+outputLatency`. Lưu theo model máy.
- Tham khảo: tài liệu Android "Audio latency" + Oboe/OboeTester docs; Apple Audio Session Programming Guide.

**3.2 Graph & real-time**
- Đồ thị cố định, mọi node cấp phát ở `prepare()`. Tham số: `std::atomic<float>` + làm mượt one-pole (~20 ms). Lệnh cấu trúc (đổi preset) qua SPSC queue; preset mới dựng ở luồng nền, đẩy con trỏ qua queue, audio thread crossfade 15 ms, trả object cũ về luồng nền để huỷ.
- Bật flush-to-zero chống denormal; chế độ debug có "RT checker" phát hiện malloc/lock trong callback.
- Tham khảo: Ross Bencina, *Real-time audio programming 101*; Timur Doumler, các bài nói về lock-free audio.

**3.3 Dò cao độ — chọn MPM, YIN làm đối chứng**
- **McLeod Pitch Method** (NSDF qua FFT): có sẵn chỉ số "clarity" dùng làm độ tin cậy/voicing, ít lỗi quãng 8 nhờ chọn đỉnh theo ngưỡng k≈0.9. YIN viết kèm để so trong test; chọn bản thắng theo số đo.
- Hạ mẫu 48→16 kHz bằng FIR polyphase (độ trễ ~1 ms). Cửa sổ 640 mẫu (40 ms), hop 64 mẫu (4 ms), nội suy parabol.
- Voicing: năng lượng + clarity + zero-crossing rate. Chống nhảy quãng 8: median 5 khung + chỉ chấp nhận nhảy lớn khi giữ ổn định ≥ 3 khung.
- Tham khảo: McLeod & Wyvill 2005; de Cheveigné & Kawahara 2002 (YIN); Mauch & Dixon 2014 (pYIN, ý tưởng voicing).

**3.4 Chỉnh giọng — tự viết, TD-PSOLA**
- Lượng tử theo scale + hysteresis (vùng chết ~30 cent quanh ranh giới nốt). Đường cong chỉnh làm mượt bậc 2 với hằng số thời gian = `retuneSpeed`.
- Tách f0 thành phần chậm (lọc thông thấp ~3 Hz) và vibrato 4–8 Hz → chỉ chỉnh phần chậm (`vibratoPreserve`). `flexAmount` = ngưỡng cent không chỉnh. `humanize` = nốt ngân ổn định lâu thì chỉnh chậm dần.
- **Rất quan trọng với nhạc Việt**: luyến láy bolero và dấu thanh (hỏi, ngã) tạo đường cong cao độ — preset "Tự nhiên" phải giữ được, sẽ kiểm bằng nghe A/B trên giọng mẫu.
- PSOLA: đánh dấu chu kỳ theo f0, grain Hann 2 chu kỳ, độ trễ cố định 10 ms; đoạn unvoiced đi thẳng có crossfade.
- Tham khảo: Moulines & Charpentier 1990 (PSOLA); Zölzer, *DAFX* (chương pitch shifting).

**3.5 Đổi tone beat — phase vocoder tự viết**
- STFT 4096/hop 1024 @48 kHz, **phase locking kiểu identity** (Laroche & Dolson 1999) hoặc **PGHI** (Průša & Holighaus 2017); phát hiện transient bằng spectral flux → khởi tạo lại pha tại transient (Röbel 2003) để trống không nhòe; pitch = time-stretch + resample.
- Render trước cả bài ở luồng nền ra file tạm int16; trong lúc chờ, phát bản streaming. Cache theo mức tone. So A/B với Signalsmith Stretch.
- Đổi tone → engine tự cộng nửa cung vào key của chỉnh giọng.

**3.6 Dò tone / BPM / LUFS**
- **Ước lượng tuning trước** (beat karaoke Việt hay lệch A4 ≠ 440 Hz) → dùng cho CQT và cho chỉnh giọng.
- HPSS bằng median filter (Fitzgerald 2010) → CQT (Schörkhuber & Klapuri 2010) → chroma có trọng số dải bass.
- So khớp profile Krumhansl-Kessler / Temperley / EDMA (Faraldo 2016) → chọn bằng điểm trên tập dev.
- Theo dõi chuyển tone: HMM 24 trạng thái trên đoạn 4–8 s, ma trận chuyển ưu tiên ở lại + họ hàng gần (quãng 5, tương đối, **song song** — bolero hay chuyển Am → A ở điệp khúc), giải Viterbi.
- BPM: onset strength từ spectral flux + autocorrelation + prior tempo (Percival & Tzanetakis 2014; Ellis 2007). LUFS: ITU-R BS.1770-4.
- Tham khảo thêm: Krumhansl 1990; Temperley 2007; Bello et al. 2005 (onset tutorial); Harte & Sandler 2005 (tuning/chroma).

**3.7 FX**: biquad theo **RBJ Audio EQ Cookbook**, SVF/TPT (Zavalishin, *The Art of VA Filter Design*) cho filter cần điều chế; compressor RMS + soft-knee (Giannoulis, Massberg & Reiss 2012, JAES); de-esser = compressor có sidechain băng 5–9 kHz; delay dòng trễ phân số + LPF trong vòng feedback, sync BPM; reverb **FDN 8 dòng, ma trận Householder/Hadamard** (Jot & Chaigne 1991); limiter: true-peak có lookahead **1 ms** (giữ độ trễ thấp).

**3.8 Thu âm**: audio thread ghi vào SPSC ring → luồng ghi viết WAV 48 kHz (dry, wet, beat) đồng bộ theo bộ đếm frame; bù độ trễ đo được bằng cách dịch track giọng. AAC: NDK `AMediaCodec`/`AMediaMuxer`, iOS `ExtAudioFile`/AVAssetWriter.

**3.9 C API**: handle mờ (opaque), mã lỗi, hàm `tc_get_api_version`. Sự kiện về UI theo kiểu **kéo**: Dart gọi `tc_poll_events` / `tc_get_metrics` mỗi frame; kết quả phân tích dài dùng `NativeCallable.listener` (Dart 3.1+) — không bao giờ gọi từ audio thread.

**Fingerprint**: chòm sao đỉnh phổ + băm cặp đỉnh (Wang 2003) — sẽ ghi vào `patent-review.md` để luật sư xem.

---

## 4. Năm rủi ro kỹ thuật lớn nhất

| # | Rủi ro | Giảm thiểu |
|---|---|---|
| 1 | **Độ trễ Android** tầm trung vượt chỉ tiêu (Samsung A-series, máy không hỗ trợ MMAP) | Đo trên máy thật ngay tuần 1–2 M1 (go/no-go); VoicePerformance + Exclusive + 48 kHz native; ADPF hint; buffer tự điều chỉnh; dự phòng: chế độ soundcard OTG nghe lại bằng phần cứng, cảnh báo rõ cho người dùng |
| 2 | **Chỉnh giọng nghe giả / làm hỏng luyến láy, dấu thanh tiếng Việt**; lỗi quãng 8 với giọng hơi, giọng khàn | Preset Tự nhiên chỉnh nhẹ (flex + humanize + giữ vibrato); bộ giọng mẫu Việt + nghe A/B mù; unit test sai số ≤ 5 cent; voicing chặt |
| 3 | **Dò tone < 0,85 MIREX** do ngũ cung/bolero mơ hồ trưởng-thứ, beat lệch tuning, chuyển tone song song | Ước lượng tuning; HPSS + chroma bass; chọn profile trên tập dev riêng (không trên 50 bài test); HMM có chuyển song song; cho sửa tay + kho tone dùng chung; CNN ONNX nếu cần |
| 4 | **CPU/nhiệt**: PSOLA + FX + FDN + đổi tone beat trên máy tầm trung, 30 phút → xrun khi máy nóng hoặc luồng bị đẩy sang nhân yếu | Render trước beat ở nền; NEON SIMD; bench từng node; mức chất lượng theo máy (reverb 4 dòng cho máy yếu); soak test 30 phút tự động có đếm xrun |
| 5 | **Route & xung đột hệ sinh thái**: Bluetooth trễ 150–300 ms, soundcard OTG lạ (chỉ 44,1 kHz, map kênh sai), plugin Flutter (camera, video_player) tranh AVAudioSession / audio focus | Native giữ quyền audio session; camera chạy không tiếng; xử lý đổi route; resample nội bộ khi thiết bị không hỗ trợ 48 kHz; danh sách máy + soundcard đã kiểm |

Rủi ro phụ: license trọng số Demucs; chi phí GPU server (CPU khó đạt 4 phút ≤ 60 s, GPU loại T4 làm được); thiếu kỹ sư backend.

---

## 5. Ước lượng thời gian (1 kỹ sư DSP + 1 kỹ sư mobile)

| # | DSP (tuần) | Mobile (tuần) | Ghi chú |
|---|---|---|---|
| M0 | 2 | 2 | làm song song |
| M1 | 4 | 3 | mobile: Flutter shell, audio session iOS, đi đo máy |
| M2 | 6 | 1 | mobile: đồng hồ cao độ, 3 nút |
| M3 | 6 | — | phụ thuộc bộ dữ liệu có nhãn |
| M4 | 4 | 1 | |
| M5 | 5 | 1 | |
| M6 | 0,5 | 4 | mobile kiêm server, làm song song M3–M4 |
| M7 | 1 | 5 | xuất video native là phần nặng |
| M8 | 0,5 | 5 | song song M5 |
| M9 | 4 | 4 | test 13 máy |

- Tổng công DSP ≈ **33 tuần**, mobile ≈ **26 tuần**. Đường găng là kỹ sư DSP: M0→M1→M2→M3→M4→M5→M9.
- Lịch thực tế khi làm song song: **~8 tháng**, cộng 20% dự phòng → **~9,5–10 tháng** đến sẵn sàng phát hành.
- Mốc demo sớm có giá trị: hết M2 (~tháng 3) đã hát được có chỉnh giọng với tone chọn tay.

---

## Khi được duyệt: M0 sẽ làm gì
1. `git init`, tạo cây thư mục, lưu spec thành `CLAUDE.md`, tạo `docs/licenses.md`, `docs/references.md` (từ mục 3 ở trên), ADR-001 (C API + FFI), ADR-002 (chỉ tiêu độ trễ, theo quyết định của bạn ở điểm 1).
2. Engine: khung CMake + presets, `tonecore.h` với `tc_get_api_version`, `tc_engine_create/destroy` (backend null), `spsc_ring` + `smoothed_param` + unit test, một bench mẫu.
3. Plugin `tonecore_flutter` (ffigen), app Flutter rỗng hiển thị "ToneCore vX – engine OK" sau khi gọi `tc_engine_create`.
4. CI GitHub Actions 4 job như mục 1, chạy cả ASan/UBSan/TSan.

## Kiểm tra M0 (cách bạn tự xác nhận)
- `cmake --preset host-macos && cmake --build --preset host-macos && ctest --preset host-macos` → tất cả test xanh.
- `cd app && fvm flutter run` trên Android emulator và iOS simulator → màn hình hiện version engine.
- Tab Actions trên GitHub: 4 job xanh.
