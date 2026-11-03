import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Phần native của ChuẩnTone (M7, docs/m7-plan.md) qua kênh `chuantone/media`. Chỉ có trên iOS ở
/// M7; nền tảng khác → [ChuanToneMedia.supported] = false.
abstract final class ChuanToneMedia {
  static const _channel = MethodChannel('chuantone/media');

  /// Có phần native trên nền tảng này không (bị đặt false khi gọi mà chưa có plugin).
  static bool supported = true;

  /// Dựng video 9:16 nền sóng nhạc + tên bài từ file âm thanh [audio] (M4A đã xuất) → MP4
  /// [output] (1080p; [hd720] = 720p). Trả true khi xong, false khi bị [cancel]; null nếu nền tảng chưa hỗ trợ. Lỗi →
  /// [PlatformException]. Theo dõi bằng [progress].
  static Future<bool?> renderWaveformVideo({
    required String audio,
    required String output,
    required String title,
    String subtitle = '',
    int color = 0xFFE5484D,
    bool hd720 = false,
  }) async {
    try {
      return await _channel.invokeMethod<bool>('renderWaveformVideo', {
        'audio': audio,
        'output': output,
        'title': title,
        'subtitle': subtitle,
        'color': color,
        'hd720': hd720,
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

  // ---- Camera trước (B6) ----

  /// Xin quyền + bật camera trước (xem trước bằng [CameraPreviewView]). Lỗi / không được phép →
  /// [PlatformException]; null = nền tảng chưa hỗ trợ.
  static Future<bool?> cameraStart() async {
    try {
      return await _channel.invokeMethod<bool>('cameraStart');
    } on MissingPluginException {
      supported = false;
      return null;
    }
  }

  static Future<void> cameraStop() async {
    try {
      await _channel.invokeMethod<void>('cameraStop');
    } on MissingPluginException {
      // không có camera để tắt
    }
  }

  /// Bắt đầu ghi hình (không tiếng) vào [path] từ khung hình kế tiếp.
  static Future<void> cameraRecordStart(String path) =>
      _channel.invokeMethod<void>('cameraRecordStart', {'path': path});

  /// Dừng ghi hình; trả đoạn đã ghi (mốc khung đầu theo đồng hồ máy).
  static Future<CameraClip> cameraRecordStop() async {
    final m = await _channel.invokeMapMethod<String, Object?>(
      'cameraRecordStop',
    );
    return CameraClip.fromMap(m ?? const {});
  }

  /// Ghép hình [video] (không tiếng) + tiếng [audio] → MP4 [output]. [offsetNs]: thời điểm (trên
  /// trục của file tiếng) khung hình đầu tiên khớp; âm = hình bắt đầu trước tiếng (bị cắt).
  /// [hd720]: nén lại hình về 720p (nhẹ hơn khi gửi Zalo), mặc định giữ 1080p.
  static Future<void> muxVideo({
    required String video,
    required String audio,
    required String output,
    required int offsetNs,
    bool hd720 = false,
  }) => _channel.invokeMethod<void>('muxVideo', {
    'video': video,
    'audio': audio,
    'output': output,
    'offsetNs': offsetNs,
    'hd720': hd720,
  });

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

/// Đoạn hình camera đã ghi.
class CameraClip {
  const CameraClip({
    required this.path,
    required this.firstFrameNs,
    required this.duration,
    required this.frames,
    required this.dropped,
  });

  factory CameraClip.fromMap(Map<String, Object?> m) => CameraClip(
    path: m['path'] as String? ?? '',
    firstFrameNs: (m['firstFrameNs'] as num?)?.toInt() ?? 0,
    duration: Duration(
      microseconds: (((m['durationSec'] as num?) ?? 0) * 1e6).round(),
    ),
    frames: (m['frames'] as num?)?.toInt() ?? 0,
    dropped: (m['dropped'] as num?)?.toInt() ?? 0,
  );

  final String path;

  /// Mốc khung hình đầu theo đồng hồ máy (ns, cùng gốc với `RecordStatus.startHostTimeNs`).
  final int firstFrameNs;
  final Duration duration;
  final int frames;
  final int dropped;
}

/// Khung xem trước camera trước (iOS). Nền tảng khác: ô đen.
class CameraPreviewView extends StatelessWidget {
  const CameraPreviewView({super.key});

  @override
  Widget build(BuildContext context) {
    if (defaultTargetPlatform != TargetPlatform.iOS) {
      return const ColoredBox(color: Color(0xFF000000));
    }
    return const UiKitView(viewType: 'chuantone/camera-preview');
  }
}

/// File nhạc mở từ app khác ("Mở bằng ChuẩnTone" từ Zalo, Tệp, Safari, AirDrop — M8 B1), qua kênh
/// `chuantone/incoming` (iOS: plugin này; Android: `MainActivity`). Phần native đã chép file vào
/// thư mục tạm của app; app chuyển nó vào thư viện.
abstract final class IncomingFiles {
  static const _channel = MethodChannel('chuantone/incoming');

  /// Bắt đầu nhận: [onFiles] được gọi với các file đang chờ — cả file làm app mở lên — và mỗi khi
  /// có file mới.
  static void listen(void Function(List<IncomingFile> files) onFiles) {
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'available') {
        final files = await take();
        if (files.isNotEmpty) onFiles(files);
      }
    });
    take().then((files) {
      if (files.isNotEmpty) onFiles(files);
    });
  }

  /// Lấy (và xoá khỏi hàng chờ) các file đang chờ; nền tảng chưa hỗ trợ → rỗng.
  static Future<List<IncomingFile>> take() async {
    try {
      final list =
          await _channel.invokeListMethod<Map<Object?, Object?>>('take') ??
          const [];
      return [
        for (final m in list)
          if (m['path'] case final String path)
            IncomingFile(
              path: path,
              name: m['name'] as String? ?? path.split('/').last,
            ),
      ];
    } on MissingPluginException {
      return const [];
    } on PlatformException {
      return const [];
    }
  }
}

/// Một file nhận từ app khác: [path] = bản chép trong thư mục tạm, [name] = tên file gốc.
class IncomingFile {
  const IncomingFile({required this.path, required this.name});

  final String path;
  final String name;
}
