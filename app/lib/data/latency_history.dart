import 'dart:convert';
import 'dart:io';

/// Lưu lịch sử đo độ trễ (các dòng CSV) vào một file JSON trong thư mục dữ liệu riêng của app.
///
/// Không dùng plugin: thư mục tạm của Dart nằm trong vùng dữ liệu của app
/// (Android: `<data>/cache`, iOS: `<container>/tmp`), nên thư mục cha của nó là vùng dữ liệu
/// bền vững của app (Android: `<data>/files`, iOS: `<container>/Library/Application Support`).
class LatencyHistory {
  LatencyHistory({this.baseDir});

  static const maxEntries = 50;

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  Directory _dir() {
    final base = baseDir;
    if (base != null) return base;
    final appRoot = Directory.systemTemp.parent;
    if (Platform.isIOS) {
      return Directory('${appRoot.path}/Library/Application Support');
    }
    if (Platform.isAndroid) return Directory('${appRoot.path}/files');
    return Directory.systemTemp;
  }

  File get _file => File('${_dir().path}/latency_history.json');

  Future<List<String>> load() async {
    try {
      final f = _file;
      if (!await f.exists()) return [];
      final data = jsonDecode(await f.readAsString());
      if (data is List) return data.whereType<String>().toList();
    } catch (_) {
      // File hỏng → coi như chưa có lịch sử.
    }
    return [];
  }

  /// Thêm dòng mới lên đầu, giữ tối đa [maxEntries] dòng. Trả về danh sách mới.
  Future<List<String>> add(String line, List<String> current) async {
    final updated = [line, ...current].take(maxEntries).toList();
    final dir = _dir();
    await dir.create(recursive: true);
    // Ghi file tạm rồi đổi tên để không làm hỏng file cũ nếu app bị tắt giữa chừng.
    final tmp = File('${_file.path}.tmp');
    await tmp.writeAsString(jsonEncode(updated), flush: true);
    await tmp.rename(_file.path);
    return updated;
  }
}
