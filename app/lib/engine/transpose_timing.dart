import 'labels.dart';

/// Đo đổi tone beat trên máy thật (M4 B7) để đọc ở "Thông số kỹ thuật" của Phòng thử.
///
/// - Bấm → nghe tone mới: tính từ lần bấm cuối tới khi engine báo `audibleTranspose` bằng mức đã
///   bấm. Trang đọc trạng thái ~30 lần/giây nên số đo lệch tối đa ~33 ms.
/// - Render cả bài: engine báo trong sự kiện `transposeReady` (đo ở luồng nền).
class TransposeTiming {
  int? _pending;
  Duration _requestedAt = Duration.zero;
  int? _requested;

  /// Thời gian bấm → nghe của lần đổi gần nhất.
  Duration? lastSwitch;

  /// Mức tone của [lastSwitch].
  int? lastSwitchSemitones;

  /// Thời gian render cả bài ở mức tone đang chọn (null khi chưa xong hoặc đang ở tone gốc).
  Duration? lastRender;

  /// Đang chờ nghe được mức vừa bấm.
  bool get pending => _pending != null;

  /// Người dùng vừa bấm đổi sang [semitones] lúc [at].
  void requested(int semitones, Duration at) {
    _pending = semitones;
    _requested = semitones;
    _requestedAt = at;
    lastRender = null;
  }

  /// Gọi mỗi lần đọc trạng thái phát. Trả true khi vừa đo xong một lần đổi.
  bool observe(int audibleTranspose, Duration at) {
    final p = _pending;
    if (p == null || audibleTranspose != p) return false;
    lastSwitch = at - _requestedAt;
    lastSwitchSemitones = p;
    _pending = null;
    return true;
  }

  /// Engine đã render xong cả bài ở [semitones] trong [ms] mili giây.
  void rendered(int semitones, double ms) {
    if (semitones != _requested) return; // bản cũ (người dùng đã bấm mức khác)
    lastRender = Duration(microseconds: (ms * 1000).round());
  }

  /// Beat mới: bỏ số đo của bài trước.
  void reset() {
    _pending = null;
    _requested = null;
    lastSwitch = null;
    lastSwitchSemitones = null;
    lastRender = null;
  }
}

/// "nghe sau 40 ms (+2) · cả bài 3.1 s" — dòng số đo ở Phòng thử.
String transposeTimingLabel(TransposeTiming t) {
  final sw = t.lastSwitch;
  if (sw == null) return t.pending ? 'đang đổi…' : 'chưa đổi';
  final render = t.lastRender;
  final whole = render == null
      ? ''
      : ' · cả bài ${(render.inMilliseconds / 1000).toStringAsFixed(1)} s';
  return 'nghe sau ${sw.inMilliseconds} ms '
      '(${transposeLabel(t.lastSwitchSemitones!)})$whole';
}
