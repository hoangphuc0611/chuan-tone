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

## Dự kiến (chưa thêm)

| Thư viện | License | Milestone | Ghi chú |
|---|---|---|---|
| Oboe | Apache-2.0 | M1 | Audio I/O Android |
| PFFFT | BSD-like (FFTPACK) | M2 | FFT SIMD; iOS có thể dùng vDSP |
| dr_wav / dr_mp3 / dr_flac | Public domain / MIT-0 | M1 | chỉ cho test/CLI |
| nlohmann/json | MIT | M5 | preset JSON |
| libFLAC | BSD-3-Clause | M6 | nén trước khi upload |
| ONNX Runtime | MIT | sau MVP | model on-device |
| riverpod, drift, permission_handler | MIT | M1+ | app |
| path_provider, camera, share_plus, go_router | BSD-3-Clause | M1+ | app |
| FastAPI / uvicorn / PyTorch / Demucs (code) | MIT / BSD-3 / BSD-3 / MIT | M6 | server – **trọng số htdemucs cần xác minh license** |
| arq / Valkey | MIT / BSD-3 | M6 | hàng đợi job |
| numpy / mir_eval / matplotlib | BSD / MIT / PSF-BSD | M3 | chỉ trong `tools/` |
| Signalsmith Stretch | MIT | M4 | chỉ để so A/B chất lượng |

## Đã loại

Rubber Band (GPL), SoundTouch (LGPL), aubio (GPL), Essentia (AGPL), libKeyFinder (GPL), Chromaprint (LGPL), ffmpeg / ffmpeg_kit (LGPL/GPL), libsndfile (LGPL), model madmom (CC BY-NC-SA), Redis ≥ 7.4 (RSAL/SSPL/AGPL).
