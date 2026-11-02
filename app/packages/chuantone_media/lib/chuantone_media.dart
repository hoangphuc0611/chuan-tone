import 'package:flutter/services.dart';

/// Phần native của ChuẩnTone (M7, docs/m7-plan.md) qua kênh `chuantone/media`. Chỉ có trên iOS ở
/// M7; nền tảng khác → [ChuanToneMedia.supported] = false.
abstract final class ChuanToneMedia {
  static const _channel = MethodChannel('chuantone/media');

  /// Có phần native trên nền tảng này không (bị đặt false khi gọi mà chưa có plugin).
  static bool supported = true;

  /// Dựng video 9:16 nền sóng nhạc + tên bài từ file âm thanh [audio] (M4A đã xuất) → MP4
  /// [output]. Trả true khi xong, false khi bị [cancel]; null nếu nền tảng chưa hỗ trợ. Lỗi →
  /// [PlatformException]. Theo dõi bằng [progress].
  static Future<bool?> renderWaveformVideo({
    required String audio,
    required String output,
    required String title,
    String subtitle = '',
    int color = 0xFFE5484D,
  }) async {
    try {
      return await _channel.invokeMethod<bool>('renderWaveformVideo', {
        'audio': audio,
        'output': output,
        'title': title,
        'subtitle': subtitle,
        'color': color,
      });
    } on MissingPluginException {
      supported = false;
      return null;
    }
  }

  /// Tiến độ (0..1) của việc dựng video đang chạy.
  static Future<double> progress() async {
    try {
      return await _channel.invokeMethod<double>('progress') ?? 0;
    } on MissingPluginException {
      return 0;
    }
  }

  /// Huỷ việc dựng video đang chạy.
  static Future<void> cancel() async {
    try {
      await _channel.invokeMethod<void>('cancel');
    } on MissingPluginException {
      // không có gì để huỷ
    }
  }

  /// Lưu video vào Ảnh. true = đã lưu, false = người dùng không cho phép; null = chưa hỗ trợ.
  static Future<bool?> saveVideoToPhotos(String path) async {
    try {
      return await _channel.invokeMethod<bool>('saveVideoToPhotos', {
        'path': path,
      });
    } on MissingPluginException {
      supported = false;
      return null;
    }
  }

  /// Mở bảng chia sẻ của hệ điều hành với file [path]. Trả true nếu người dùng đã chọn một đích,
  /// false nếu đóng bảng; null nếu nền tảng chưa hỗ trợ.
  static Future<bool?> share(String path) async {
    try {
      return await _channel.invokeMethod<bool>('share', {'path': path});
    } on MissingPluginException {
      supported = false;
      return null;
    }
  }
}
