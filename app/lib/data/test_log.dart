import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'app_dirs.dart';
import 'app_settings.dart';

/// Nhật ký thử nghiệm (M8 B7, docs/m8-plan.md mục 7): bật ở Cài đặt → Nâng cao. Ghi màn hình đã vào,
/// thao tác chính, lỗi — mỗi dòng một sự kiện JSON có mốc thời gian — vào file trên máy, không gửi
/// lên mạng, không ghi tên bài. Xuất bằng bảng chia sẻ → AirDrop sang Mac; `tools/usability/` tính
/// thời gian từng nhiệm vụ của buổi thử.
abstract final class TestLog {
  static bool _enabled = false;
  static Directory? _baseDir;
  static Future<void> _writing = Future.value();
  static final _lastAt = <String, DateTime>{};

  /// Đồng hồ (test thay được).
  static DateTime Function() now = DateTime.now;

  static bool get enabled => _enabled;

  /// File nhật ký (`<dữ liệu app>/nhat-ky-thu-nghiem.jsonl`).
  static File get file =>
      File('${(_baseDir ?? appDataDir()).path}/nhat-ky-thu-nghiem.jsonl');

  /// Đọc cài đặt lúc mở app; đang bật thì ghi mốc phiên mới kèm [session] (phiên bản, tên máy).
  static Future<void> init({
    Directory? baseDir,
    Map<String, Object?> session = const {},
  }) async {
    _baseDir = baseDir;
    _enabled = await AppSettings(baseDir: baseDir).testLog();
    if (_enabled) event('session', session);
  }

  /// Bật / tắt (lưu cài đặt).
  static Future<void> setEnabled(
    bool on, {
    Map<String, Object?> session = const {},
  }) async {
    await AppSettings(baseDir: _baseDir).setTestLog(on);
    if (on && !_enabled) {
      _enabled = true;
      event('session', session);
    } else if (!on && _enabled) {
      event('log.off');
      _enabled = false;
    }
  }

  /// Vào màn [name].
  static void screen(String name) => event('screen', {'name': name});

  /// Ghi sự kiện [type] kèm [data]. [throttle]: bỏ các lần cùng loại trong 0,5 s (thanh trượt kéo
  /// liên tục) — vẫn giữ lần đầu.
  static void event(
    String type, [
    Map<String, Object?> data = const {},
    bool throttle = false,
  ]) {
    if (!_enabled) return;
    final t = now();
    if (throttle) {
      final last = _lastAt[type];
      if (last != null &&
          t.difference(last) < const Duration(milliseconds: 500)) {
        return;
      }
      _lastAt[type] = t;
    }
    final line = jsonEncode({'t': t.toIso8601String(), 'e': type, ...data});
    final f = file;
    _writing = _writing.then((_) async {
      try {
        await f.parent.create(recursive: true);
        await f.writeAsString('$line\n', mode: FileMode.append);
      } catch (_) {
        // ghi không được thì thôi: nhật ký không được làm hỏng việc chính
      }
    });
  }

  /// Lỗi người dùng thấy (câu thường) + chữ kỹ thuật.
  static void error(String message, {String? details}) =>
      event('error', {'message': message, 'details': ?details});

  /// Chờ ghi xong các dòng đang đợi (trước khi chia sẻ / xoá / trong test).
  static Future<void> flush() => _writing;

  /// Kích thước file (byte), 0 nếu chưa có.
  static Future<int> size() async {
    await flush();
    final f = file;
    return await f.exists() ? f.length() : 0;
  }

  static Future<void> clear() async {
    await flush();
    final f = file;
    if (await f.exists()) await f.delete();
  }

  /// Về trạng thái ban đầu (test).
  static void reset() {
    _enabled = false;
    _baseDir = null;
    _lastAt.clear();
    now = DateTime.now;
  }
}
