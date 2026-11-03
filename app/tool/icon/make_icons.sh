#!/bin/bash
# Tạo icon + hình màn khởi động các cỡ cho iOS / Android từ PNG của render_icon_test.dart (M8 B5).
# Dùng `sips` có sẵn của macOS. Chạy từ thư mục app/:
#   fvm flutter test tool/icon/render_icon_test.dart && tool/icon/make_icons.sh
set -euo pipefail
src=build/icon
ios=ios/Runner/Assets.xcassets
res=android/app/src/main/res

# iOS: icon không được có kênh trong suốt (App Store) → qua JPEG chất lượng cao để bỏ kênh alpha.
sips -s format jpeg -s formatOptions 100 "$src/icon_1024.png" --out "$src/icon_1024.jpg" >/dev/null
sips -s format png "$src/icon_1024.jpg" --out "$src/icon_1024_opaque.png" >/dev/null
ios_icon() { sips -z "$2" "$2" "$src/icon_1024_opaque.png" --out "$ios/AppIcon.appiconset/$1" >/dev/null; }
ios_icon Icon-App-1024x1024@1x.png 1024
ios_icon Icon-App-20x20@1x.png 20
ios_icon Icon-App-20x20@2x.png 40
ios_icon Icon-App-20x20@3x.png 60
ios_icon Icon-App-29x29@1x.png 29
ios_icon Icon-App-29x29@2x.png 58
ios_icon Icon-App-29x29@3x.png 87
ios_icon Icon-App-40x40@1x.png 40
ios_icon Icon-App-40x40@2x.png 80
ios_icon Icon-App-40x40@3x.png 120
ios_icon Icon-App-60x60@2x.png 120
ios_icon Icon-App-60x60@3x.png 180
ios_icon Icon-App-76x76@1x.png 76
ios_icon Icon-App-76x76@2x.png 152
ios_icon Icon-App-83.5x83.5@2x.png 167

# iOS màn khởi động: hình trắng 128 pt giữa nền đỏ (LaunchScreen.storyboard).
sips -z 128 128 "$src/splash_384.png" --out "$ios/LaunchImage.imageset/LaunchImage.png" >/dev/null
sips -z 256 256 "$src/splash_384.png" --out "$ios/LaunchImage.imageset/LaunchImage@2x.png" >/dev/null
cp "$src/splash_384.png" "$ios/LaunchImage.imageset/LaunchImage@3x.png"

# Android: icon cũ (tròn), lớp trước của icon thích ứng (108 dp), hình màn khởi động (96 dp).
for pair in mdpi:1 hdpi:1.5 xhdpi:2 xxhdpi:3 xxxhdpi:4; do
  d=${pair%%:*}
  k=${pair##*:}
  px() { python3 -c "print(round($1 * $k))"; }
  mkdir -p "$res/mipmap-$d" "$res/drawable-$d"
  sips -z "$(px 48)" "$(px 48)" "$src/legacy_192.png" --out "$res/mipmap-$d/ic_launcher.png" >/dev/null
  sips -z "$(px 108)" "$(px 108)" "$src/foreground_432.png" \
    --out "$res/mipmap-$d/ic_launcher_foreground.png" >/dev/null
  sips -z "$(px 96)" "$(px 96)" "$src/splash_384.png" --out "$res/drawable-$d/splash_logo.png" >/dev/null
done
echo "Đã tạo icon iOS / Android và hình màn khởi động."
