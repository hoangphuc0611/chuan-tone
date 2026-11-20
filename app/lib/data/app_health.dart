import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'app_dirs.dart';

/// Số phiên dùng, số lần thoát bất thường, nhật ký lỗi — chỉ nằm trên máy (M9 B5, docs/m9-plan.md).
/// Không dùng dịch vụ báo crash bên ngoài; người dùng tự bấm gửi ở Cài đặt → Nâng cao.
///
/// Thoát bất thường: app ra trước màn hình thì đặt cờ "đang chạy", vào nền thì xoá. Mở app mà cờ
/// còn = lần trước app chết khi đang mở (crash, hoặc hệ điều hành giết vì hết bộ nhớ). Vuốt tắt app
/// từ màn đa nhiệm không tính (app đã vào nền trước). Crash-free = 1 − thoát bất thường / phiên.
///
/// Không bắt được crash native (C++ / Swift): phần đó xem trong báo cáo phân tích của hệ điều hành.
abstract final class AppHealth {
  static Directory? _baseDir;
  static AppHealthStats _stats = const AppHealthStats();
  static bool _running = false;
  static Future<void> _writing = Future.value();

  /// Đồng hồ (test thay được).
  static DateTime Function() now = DateTime.now;

  /// Nhật ký lỗi lớn hơn mức này thì bỏ nửa cũ.
  static const maxLogBytes = 200 * 1024;

  static AppHealthStats get stats => _stats;

  static File get _statsFile => File('${_dir.path}/suc-khoe-app.json');

  /// Nhật ký lỗi (`<dữ liệu app>/nhat-ky-loi.txt`).
  static File get logFile => File('${_dir.path}/nhat-ky-loi.txt');

  static Directory get _dir => _baseDir ?? appDataDir();

  /// Mọi thay đổi đi qua một hàng đợi tuần tự ([_writing]): app vào nền ngay lúc vừa mở (trước khi
  /// đọc xong file) cũng không ghi đè số đếm cũ về 0.
  static Future<void> _enqueue(FutureOr<void> Function() step) {
    return _writing = _writing.then((_) => step()).catchError((_) {
      // ghi không được thì thôi: không được làm hỏng việc chính
    });
  }

  /// Lúc mở app (đang ở trước màn hình): đếm lần thoát bất thường của lần trước, bắt đầu phiên mới.
  static Future<void> init({Directory? baseDir}) {
    _baseDir = baseDir;
    _running = false;
    _enqueue(() async {
      var s = const AppHealthStats();
      var wasRunning = false;
      try {
        final f = _statsFile;
        if (await f.exists()) {
          final j = jsonDecode(await f.readAsString()) as Map<String, Object?>;
          s = AppHealthStats.fromJson(j);
          wasRunning = j['running'] == true;
        }
      } catch (_) {
        // file hỏng: đếm lại từ đầu
      }
      if (wasRunning) {
        s = s.copyWith(
          abnormalExits: s.abnormalExits + 1,
          lastAbnormalAt: s.lastSessionAt,
        );
      }
      _stats = s;
    });
    return foreground();
  }

  /// App ra trước màn hình (mở app / quay lại từ nền): một phiên mới.
  static Future<void> foreground() => _enqueue(() async {
    if (_running) return;
    _running = true;
    _stats = _stats.copyWith(
      sessions: _stats.sessions + 1,
      lastSessionAt: now(),
    );
    await _write();
  });

  /// App vào nền: phiên này kết thúc bình thường.
  static Future<void> background() => _enqueue(() async {
    if (!_running) return;
    _running = false;
    await _write();
  });

  /// Ghi một lỗi chưa bắt (kèm mốc thời gian) vào nhật ký lỗi.
  static void recordError(String kind, String details) {
    final entry = '[${now().toIso8601String()}] $kind\n${details.trim()}\n\n';
    unawaited(
      _enqueue(() async {
        _stats = _stats.copyWith(errors: _stats.errors + 1);
        final f = logFile;
        await f.parent.create(recursive: true);
        await f.writeAsString(entry, mode: FileMode.append);
        if (await f.length() > maxLogBytes) {
          final text = await f.readAsString();
          // Giữ nửa mới, cắt ở đầu một mục.
          var cut = text.length ~/ 2;
          final next = text.indexOf('\n[', cut);
          cut = next < 0 ? cut : next + 1;
          await f.writeAsString(text.substring(cut));
        }
        await _write();
      }),
    );
  }

  static Future<void> _write() async {
    final f = _statsFile;
    await f.parent.create(recursive: true);
    await f.writeAsString(
      jsonEncode({..._stats.toJson(), 'running': _running}),
    );
  }

  /// Chờ ghi xong (trước khi chia sẻ / trong test).
  static Future<void> flush() => _writing;

  /// Nội dung để gửi nhà phát triển: dòng tóm tắt + nhật ký lỗi.
  static Future<String> report() async {
    await flush();
    final f = logFile;
    final log = await f.exists() ? await f.readAsString() : '';
    return '${_stats.summary}\n\n${log.isEmpty ? 'Chưa có lỗi nào được ghi.' : log}';
  }

  /// Xoá nhật ký lỗi và đếm lại từ đầu (phiên hiện tại tính là phiên 1).
  static Future<void> clear() => _enqueue(() async {
    final f = logFile;
    if (await f.exists()) await f.delete();
    _stats = AppHealthStats(
      sessions: _running ? 1 : 0,
      lastSessionAt: _stats.lastSessionAt,
    );
    await _write();
  });

  /// Về trạng thái ban đầu (test).
  static void reset() {
    _baseDir = null;
    _stats = const AppHealthStats();
    _running = false;
    _writing = Future.value();
    now = DateTime.now;
  }
}

class AppHealthStats {
  const AppHealthStats({
    this.sessions = 0,
    this.abnormalExits = 0,
    this.errors = 0,
    this.lastSessionAt,
    this.lastAbnormalAt,
  });

  factory AppHealthStats.fromJson(Map<String, Object?> j) => AppHealthStats(
    sessions: (j['sessions'] as num?)?.toInt() ?? 0,
    abnormalExits: (j['abnormalExits'] as num?)?.toInt() ?? 0,
    errors: (j['errors'] as num?)?.toInt() ?? 0,
    lastSessionAt: DateTime.tryParse(j['lastSessionAt'] as String? ?? ''),
    lastAbnormalAt: DateTime.tryParse(j['lastAbnormalAt'] as String? ?? ''),
  );

  final int sessions;
  final int abnormalExits;

  /// Lỗi Dart chưa bắt (không làm app chết, nhưng là lỗi cần sửa).
  final int errors;
  final DateTime? lastSessionAt;
  final DateTime? lastAbnormalAt;

  /// 0..1; null khi chưa có phiên nào.
  double? get crashFree => sessions == 0 ? null : 1 - abnormalExits / sessions;

  /// "23 phiên · 0 lần thoát bất thường · 1 lỗi"
  String get summary =>
      '$sessions phiên · $abnormalExits lần thoát bất thường · $errors lỗi';

  AppHealthStats copyWith({
    int? sessions,
    int? abnormalExits,
    int? errors,
    DateTime? lastSessionAt,
    DateTime? lastAbnormalAt,
  }) => AppHealthStats(
    sessions: sessions ?? this.sessions,
    abnormalExits: abnormalExits ?? this.abnormalExits,
    errors: errors ?? this.errors,
    lastSessionAt: lastSessionAt ?? this.lastSessionAt,
    lastAbnormalAt: lastAbnormalAt ?? this.lastAbnormalAt,
  );

  Map<String, Object?> toJson() => {
    'sessions': sessions,
    'abnormalExits': abnormalExits,
    'errors': errors,
    'lastSessionAt': ?lastSessionAt?.toIso8601String(),
    'lastAbnormalAt': ?lastAbnormalAt?.toIso8601String(),
  };
}
