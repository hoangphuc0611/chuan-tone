import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'app_dirs.dart';

/// Cài đặt chung của app (file settings.json): máy chủ tách giọng, lựa chọn "không hỏi lại" khi
/// gửi bài, mã máy ngẫu nhiên (server dùng để giới hạn số lần tách và giữ job riêng của máy).
class AppSettings {
  AppSettings({this.baseDir});

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  File get _file => File('${(baseDir ?? appDataDir()).path}/settings.json');

  Future<Map<String, dynamic>> _read() async {
    try {
      final f = _file;
      if (await f.exists()) {
        final data = jsonDecode(await f.readAsString());
        if (data is Map<String, dynamic>) return data;
      }
    } catch (_) {
      // File hỏng → mặc định.
    }
    return {};
  }

  Future<void> _set(String key, Object? value) async {
    final all = await _read();
    if (value == null) {
      all.remove(key);
    } else {
      all[key] = value;
    }
    await writeFileAtomic(_file, jsonEncode(all));
  }

  /// Địa chỉ máy chủ tách giọng, vd. "http://192.168.1.10:8000" (null = chưa đặt).
  Future<String?> separationServer() async {
    final v = (await _read())['separationServer'];
    return v is String && v.isNotEmpty ? v : null;
  }

  Future<void> setSeparationServer(String? url) =>
      _set('separationServer', url);

  /// Người dùng đã chọn "Không hỏi lại" khi đồng ý gửi bài lên để tách giọng.
  Future<bool> separationConsent() async =>
      (await _read())['separationConsent'] == true;

  Future<void> setSeparationConsent(bool remember) =>
      _set('separationConsent', remember ? true : null);

  /// Mã máy ngẫu nhiên 128 bit (tạo lần đầu, không gắn với danh tính người dùng).
  Future<String> deviceId() async {
    final v = (await _read())['deviceId'];
    if (v is String && v.length >= 16) return v;
    final rng = Random.secure();
    final id = List.generate(
      16,
      (_) => rng.nextInt(256),
    ).map((b) => b.toRadixString(16).padLeft(2, '0')).join();
    await _set('deviceId', id);
    return id;
  }
}

/// Chuẩn hoá địa chỉ người dùng nhập: thêm "http://" nếu thiếu, bỏ "/" cuối. null nếu không hợp lệ.
String? normalizeServerUrl(String input) {
  var s = input.trim();
  if (s.isEmpty) return null;
  if (!s.contains('://')) s = 'http://$s';
  final uri = Uri.tryParse(s);
  if (uri == null ||
      !(uri.scheme == 'http' || uri.scheme == 'https') ||
      uri.host.isEmpty) {
    return null;
  }
  return uri
      .replace(path: '', query: null, fragment: null)
      .toString()
      .replaceAll(RegExp(r'/+$'), '');
}
