#!/usr/bin/env bash
# Build ToneCore thành tonecore.xcframework (iPhone + Simulator) cho plugin Flutter.
# Cần Xcode đầy đủ (không chỉ Command Line Tools).
#
# Dùng:  engine/scripts/build_xcframework.sh [thư_mục_đích]
# Mặc định đích: bindings/tonecore_flutter/ios/Frameworks
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_DIR="$(cd "$ENGINE_DIR/.." && pwd)"
DEST="${1:-$REPO_DIR/bindings/tonecore_flutter/ios/Frameworks}"
WORK="$ENGINE_DIR/build/ios"
CONFIG=Release

build_sdk() {
  local sdk="$1" archs="$2"
  cmake -S "$ENGINE_DIR" -B "$WORK/$sdk" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES="$archs" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DTC_BUILD_FRAMEWORK=ON \
    -DTC_BUILD_TESTS=OFF
  cmake --build "$WORK/$sdk" --config "$CONFIG" --target tonecore -- -quiet
}

build_sdk iphoneos "arm64"
build_sdk iphonesimulator "arm64;x86_64"

rm -rf "$DEST/tonecore.xcframework"
mkdir -p "$DEST"
xcodebuild -create-xcframework \
  -framework "$WORK/iphoneos/$CONFIG-iphoneos/tonecore.framework" \
  -framework "$WORK/iphonesimulator/$CONFIG-iphonesimulator/tonecore.framework" \
  -output "$DEST/tonecore.xcframework"

echo "Đã tạo: $DEST/tonecore.xcframework"
