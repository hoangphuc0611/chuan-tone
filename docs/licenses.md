# Thư viện bên thứ ba & license

Quy tắc (CLAUDE.md mục 0.2): chỉ MIT / BSD / Apache-2.0 / Zlib / ISC. LGPL/GPL hoặc model AI có license hạn chế → phải hỏi trước. Mọi thư viện thêm vào phải được ghi ở đây trong cùng commit.

## Đang dùng

| Thư viện | Phiên bản | License | Dùng ở đâu | Có ship trong app? |
|---|---|---|---|---|
| GoogleTest | 1.15.2 | BSD-3-Clause | `engine/tests` | Không |
| Google Benchmark | 1.9.1 | Apache-2.0 | `engine/bench` | Không |
| Dart `ffi` | theo pubspec | BSD-3-Clause | `bindings/tonecore_flutter` | Có |
| Dart `ffigen` | theo pubspec | BSD-3-Clause | sinh binding (dev) | Không |
| Flutter SDK | 3.47.7 (ghim bằng `.fvmrc`) | BSD-3-Clause | `app`, `bindings` | Có |
| cupertino_icons | ^1.0.8 | MIT | `app` | Có |
| flutter_lints | ^6.0.0 | BSD-3-Clause | kiểm tra code (dev) | Không |
| Oboe | 1.11.0 (Gradle prefab) | Apache-2.0 | `engine/src/io/android` | Có |
| dr_wav / dr_mp3 | commit `dfe8377` (`engine/third_party/dr_libs/COMMIT`) | Public domain / MIT-0 | giải mã cho bản host (test, CI) | Không |
| file_picker | ^13.1.0 | MIT | `app` – chọn file beat | Có |
| device_info_plus | ^13.3.0 | BSD-3-Clause | `app` – tên máy trong báo cáo độ trễ | Có |
| mir_eval | 0.8.2 | MIT | `tools/keytest` – kiểm chéo điểm MIREX (không bắt buộc) | Không |
| numpy / scipy / decorator | theo mir_eval | BSD-3 / BSD-3 / BSD-2 | phụ thuộc của mir_eval | Không |

Lịch sử đo độ trễ lưu bằng file JSON (`app/lib/data/latency_history.dart`), không dùng `shared_preferences`: bản Android của nó gọi Kotlin qua JNI và lỗi `NoSuchMethodError` ở bản release.

Quyền micro tự làm trong plugin (Java + AVAudioSession), không dùng `permission_handler`: bản mới nhất đòi Android SDK 37 mà bộ build hiện tại (AGP 9.1) chưa hỗ trợ.

**Phụ thuộc gián tiếp cần lưu ý:** `dbus` (MPL-2.0) bị kéo vào bởi `file_picker_linux` – chỉ dùng cho bản Linux desktop, **không** được build vào app Android/iOS. Toàn bộ phụ thuộc Dart khác là MIT/BSD/Apache-2.0 (kiểm tra ngày 2026-10-20).

## Dữ liệu test (không đóng gói vào app, không commit)

Tải bằng `tools/pitch/fetch_datasets.sh` / `tools/keytest/fetch_guitarset.sh` vào `testdata/` (đã `.gitignore`). Chỉ dùng để chấm điểm và thử nghiệm trên máy dev và CI.

| Bộ dữ liệu | License | Ghi công | Dùng cho |
|---|---|---|---|
| vocadito v3 (zenodo.org/records/5578807) | CC BY 4.0 | R. M. Bittner, K. Pasalo, J. J. Bosch, G. Meseguer-Brocal, D. Rubinstein, *vocadito: A dataset of solo vocals with f0, note, and lyric annotations*, arXiv:2110.05580, 2021 | Chấm điểm dò cao độ (`tools/pitch/eval_f0.py`, CI) |
| GuitarSet 1.1.0 (zenodo.org/records/3371780) — chỉ `annotation.zip` + `audio_mono-mic.zip`, tải bằng `tools/keytest/fetch_guitarset.sh` | CC BY 4.0 (đã kiểm trên Zenodo 2026-10-23) | Q. Xi, R. M. Bittner, J. Pauwels, X. Ye, J. P. Bello, *GuitarSet: A Dataset for Guitar Transcription*, ISMIR 2018 | Kiểm tra sơ bộ dò tone / BPM (guitar solo, không phải nhạc Việt) |
| Bộ beat karaoke Việt (`testdata/vn-key/`, từ 2026-10-23) — tải từ YouTube, nguồn từng bài ở cột `url` của `dev.csv` / `test.csv` | Quyền sử dụng: chủ dự án xác nhận đã xử lý bản quyền (2026-10-23). Chỉ dùng nội bộ để kiểm thử — không commit, không lên CI, không đóng gói vào app | Kênh karaoke ghi ở cột `tieu_de` | Chỉnh và chấm dò tone (M3 B4 / B9) |
| VocalSet 1.2 (zenodo.org/records/1442513) | CC BY 4.0 | J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo, *VocalSet: A Singing Voice Dataset*, ISMIR 2018 | Thử giọng nam trầm, rung giọng, nghe A/B (chỉ máy dev, tải thủ công) |

Không dùng các bộ chỉ cho phép phi thương mại / nghiên cứu: MIR-1K, MDB-stem-synth, M4Singer, Opencpop, NUS-48E, GTSinger.

## Công cụ ngoài (không nằm trong repo, không ship)

| Công cụ | License | Dùng để |
|---|---|---|
| yt-dlp (Homebrew) | Unlicense | Tải audio cho bộ dữ liệu kiểm thử trên máy dev (script để ngoài repo; app không có tính năng tải từ YouTube — CLAUDE.md mục 8) |

## Dự kiến (chưa thêm)

| Thư viện | License | Milestone | Ghi chú |
|---|---|---|---|
| PFFFT | BSD-like (FFTPACK) | M4 (nếu cần) | M2 dùng FFT tự viết (`dsp/fft.cpp`); chỉ cân nhắc khi phase vocoder M4 cần FFT nhanh hơn |
| nlohmann/json | MIT | M5 | preset JSON |
| libFLAC | BSD-3-Clause | M6 | nén trước khi upload |
| ONNX Runtime | MIT | sau MVP | model on-device |
| riverpod, drift | MIT | M5+ | app |
| path_provider, camera, share_plus, go_router | BSD-3-Clause | M5+ | app |
| FastAPI / uvicorn / PyTorch / Demucs (code) | MIT / BSD-3 / BSD-3 / MIT | M6 | server – **trọng số htdemucs cần xác minh license** |
| arq / Valkey | MIT / BSD-3 | M6 | hàng đợi job |
| matplotlib | PSF-BSD | M3 | chỉ trong `tools/` (vẽ chroma/timeline nếu cần) |
| Signalsmith Stretch | MIT | M4 | chỉ để so A/B chất lượng |

## Đã loại

Rubber Band (GPL), SoundTouch (LGPL), aubio (GPL), Essentia (AGPL), libKeyFinder (GPL), Chromaprint (LGPL), ffmpeg / ffmpeg_kit (LGPL/GPL), libsndfile (LGPL), model madmom (CC BY-NC-SA), Redis ≥ 7.4 (RSAL/SSPL/AGPL).
