import 'dart:io';
import 'dart:math';

import 'library_db.dart';

/// Cài đặt chung của app (bảng `settings` của [LibraryDb]; trước M8 là `settings.json`): máy chủ
/// tách giọng, lựa chọn "không hỏi lại" khi gửi bài, mã máy ngẫu nhiên (server dùng để giới hạn
/// số lần tách và giữ job riêng của máy).
class AppSettings {
  AppSettings({this.baseDir});

  /// Thư mục CSDL; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  Future<Object?> _get(String key) => LibraryDb.getSetting(baseDir, key);

  Future<void> _set(String key, Object? value) =>
      LibraryDb.setSetting(baseDir, key, value);

  /// Địa chỉ máy chủ tách giọng, vd. "http://192.168.1.10:8000" (null = chưa đặt).
  Future<String?> separationServer() async {
    final v = await _get('separationServer');
    return v is String && v.isNotEmpty ? v : null;
  }

  Future<void> setSeparationServer(String? url) =>
      _set('separationServer', url);

  /// Người dùng đã chọn "Không hỏi lại" khi đồng ý gửi bài lên để tách giọng.
  Future<bool> separationConsent() async =>
      await _get('separationConsent') == true;

  Future<void> setSeparationConsent(bool remember) =>
      _set('separationConsent', remember ? true : null);

  /// Mẹo [id] (vd "sing": bong bóng mẹo ở Màn hát) chưa hiện lần nào → true và đánh dấu đã hiện;
  /// các lần sau false.
  Future<bool> firstTimeTip(String id) async {
    final key = 'tipShown.$id';
    if (await _get(key) == true) return false;
    await _set(key, true);
    return true;
  }

  /// Mã máy ngẫu nhiên 128 bit (tạo lần đầu, không gắn với danh tính người dùng).
  Future<String> deviceId() async {
    final v = await _get('deviceId');
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
