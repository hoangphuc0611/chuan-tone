#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint chuantone_media.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'chuantone_media'
  s.version          = '0.0.1'
  s.summary          = 'Phần native của ChuẩnTone (chia sẻ, video).'
  s.description      = 'Kênh chuantone/media: chia sẻ, dựng / ghép video bằng AVFoundation.'
A new Flutter plugin project.
                       DESC
  s.homepage         = 'https://github.com/hoangphuc0611/chuan-tone'
  s.license          = { :type => 'Proprietary', :file => '../LICENSE' }
  s.author           = { 'ChuẩnTone' => 'dev@chuantone.vn' }
  s.source           = { :path => '.' }
  s.source_files = 'chuantone_media/Sources/chuantone_media/**/*'
  s.dependency 'Flutter'
  s.frameworks = 'AVFoundation', 'Accelerate', 'CoreText', 'Photos'
  s.platform = :ios, '15.0'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'

end
