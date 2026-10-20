# Hướng dẫn build

## Cài đặt một lần (macOS)

```bash
brew install cmake ninja clang-format uv
brew tap leoafarias/fvm && brew install fvm
fvm install            # cài đúng bản Flutter ghi trong .fvmrc
```

Thêm cho mobile:
- **iOS**: Xcode 16+ (App Store), sau đó `sudo xcode-select -s /Applications/Xcode.app` và `sudo gem install cocoapods` (hoặc `brew install cocoapods`).
- **Android** (cách đã dùng trên máy dev, không cần Android Studio):
  ```bash
  brew install openjdk@17 && brew install --cask android-commandlinetools
  export JAVA_HOME=/opt/homebrew/opt/openjdk@17 ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
  sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.0.0" "cmake;3.22.1" "ndk;28.2.13676358"
  ```
  Hoặc Android Studio, trong SDK Manager cài *Android SDK Platform 36*, *NDK (Side by side) 28.2.13676358* (bản Flutter 3.47 yêu cầu), *CMake*. JDK 17 (đi kèm Android Studio).

## Engine (C++)

```bash
cd engine
cmake --preset host && cmake --build --preset host && ctest --preset host   # Debug + test
cmake --preset host-asan && cmake --build --preset host-asan && ctest --preset host-asan
cmake --preset host-tsan && cmake --build --preset host-tsan && ctest --preset host-tsan
cmake --preset host-release && cmake --build --preset host-release
./build/host-release/bench/tonecore_bench                                     # benchmark
```

Android: engine chỉ build qua Gradle (Gradle cung cấp Oboe dạng prefab cho CMake):
```bash
cd app && fvm flutter build apk --debug
```

iOS framework (cần Xcode đầy đủ) — **phải chạy trước** `pod install` / `flutter build ios`:
```bash
engine/scripts/build_xcframework.sh     # → bindings/tonecore_flutter/ios/Frameworks/tonecore.xcframework
```

## Binding Dart

```bash
cd bindings/tonecore_flutter
fvm dart run ffigen --config ffigen.yaml               # sinh lại binding khi đổi tonecore.h
TONECORE_LIB=../../engine/build/host/libtonecore.dylib fvm flutter test   # test FFI trên máy dev
```

## App

```bash
cd app
fvm flutter run            # chọn emulator Android hoặc simulator iOS
fvm flutter build apk --debug
fvm flutter build ios --simulator --debug
```

## CI
GitHub Actions trong `.github/workflows/`:
- `engine.yml`: test engine trên Linux (thường, ASan, UBSan, TSan) và macOS; benchmark; kiểm tra clang-format.
- `flutter.yml`: test binding Dart với thư viện host, `flutter analyze`, build APK, build iOS simulator.
- Server (`server.yml`) sẽ thêm ở M6 khi có mã server.
