/// Chẩn đoán trên máy thật: trong [window] sau mỗi lần đổi tone, vị trí phát phải tăng đều theo
/// đồng hồ. Nhảy lùi = phát lặp lại một đoạn; khựng = đứng chờ (thiếu khối đã render) hoặc kẹt.
/// Dòng tóm tắt hiện ở "Thông số kỹ thuật" của Phòng thử để đọc qua ảnh chụp màn hình.
class PlaybackWatch {
  static const window = Duration(seconds: 15);
  static const _jumpBack = Duration(milliseconds: 50);

  Duration? _start;
  Duration? _lastAt;
  Duration _lastPos = Duration.zero;
  bool _inStall = false;

  /// Số lần vị trí nhảy lùi (> 50 ms) và lần lùi xa nhất.
  int jumpsBack = 0;
  Duration maxJumpBack = Duration.zero;

  /// Số lần khựng (vị trí tăng < 30% đồng hồ) và tổng thời gian khựng.
  int stalls = 0;
  Duration stalled = Duration.zero;

  Duration _wall = Duration.zero;
  Duration _advanced = Duration.zero;

  /// Đã theo dõi xong (hết [window]) hay chưa bắt đầu.
  bool done(Duration at) => _start == null || at - _start! > window;

  /// Tốc độ vị trí so với đồng hồ khi đang phát (1.00 = đúng).
  double get rate => _wall > Duration.zero
      ? _advanced.inMicroseconds / _wall.inMicroseconds
      : 1.0;

  /// Vừa đổi tone lúc [at]: bắt đầu theo dõi lại.
  void restart(Duration at) {
    _start = at;
    _lastAt = null;
    _inStall = false;
    jumpsBack = 0;
    maxJumpBack = Duration.zero;
    stalls = 0;
    stalled = Duration.zero;
    _wall = Duration.zero;
    _advanced = Duration.zero;
  }

  /// Gọi mỗi lần đọc trạng thái phát (~30 lần/giây).
  void observe({
    required Duration at,
    required Duration position,
    required bool playing,
  }) {
    if (done(at)) return;
    if (!playing) {
      _lastAt = null; // tạm dừng: không tính
      return;
    }
    final last = _lastAt;
    if (last != null) {
      final dt = at - last;
      final dp = position - _lastPos;
      if (dp < -_jumpBack) {
        jumpsBack++;
        if (-dp > maxJumpBack) maxJumpBack = -dp;
      } else {
        _wall += dt;
        _advanced += dp;
        final stall = dp.inMicroseconds < 0.3 * dt.inMicroseconds;
        if (stall) {
          stalled += dt;
          if (!_inStall) stalls++;
        }
        _inStall = stall;
      }
    }
    _lastAt = at;
    _lastPos = position;
  }
}

/// "vị trí ×1.00 · nhảy lùi 0 · khựng 0" (+ "(đang đo)" trong 15 s đầu).
String playbackWatchLabel(PlaybackWatch w, Duration now) {
  if (w._start == null) return '—';
  final back = w.jumpsBack == 0
      ? 'nhảy lùi 0'
      : 'nhảy lùi ${w.jumpsBack} (xa nhất ${(w.maxJumpBack.inMilliseconds / 1000).toStringAsFixed(1)} s)';
  final stall = w.stalls == 0
      ? 'khựng 0'
      : 'khựng ${w.stalls} (${w.stalled.inMilliseconds} ms)';
  final measuring = w.done(now) ? '' : ' (đang đo)';
  return 'vị trí ×${w.rate.toStringAsFixed(2)} · $back · $stall$measuring';
}
