# ChuẩnTone (app Flutter)

App karaoke tự dò tone, tự chỉnh giọng. Engine âm thanh: `../engine` (ToneCore), gọi qua plugin `../bindings/tonecore_flutter`.

```bash
fvm flutter run            # Android emulator / iOS simulator
fvm flutter test           # test giao diện (không cần thư viện native)
```

iOS: chạy `../engine/scripts/build_xcframework.sh` trước lần build đầu. Chi tiết: `../docs/build.md`.
