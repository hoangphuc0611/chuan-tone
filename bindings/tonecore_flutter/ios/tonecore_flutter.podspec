#
# Plugin FFI cho ToneCore trên iOS.
# Engine được build sẵn thành tonecore.xcframework bởi engine/scripts/build_xcframework.sh
# (chạy trước `pod install` / `flutter build ios`). Dart nạp 'tonecore.framework/tonecore'.
#
Pod::Spec.new do |s|
  s.name             = 'tonecore_flutter'
  s.version          = '0.1.0'
  s.summary          = 'Binding Dart FFI cho ToneCore.'
  s.description      = 'Engine âm thanh ToneCore của ChuẩnTone, dạng dynamic xcframework.'
  s.homepage         = 'https://chuantone.vn'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'ChuẩnTone' => 'dev@chuantone.vn' }

  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.vendored_frameworks = 'Frameworks/tonecore.xcframework'
  s.dependency 'Flutter'
  s.platform = :ios, '15.0'

  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'
end
