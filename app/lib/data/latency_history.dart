import 'dart:convert';
import 'dart:io';

import 'app_dirs.dart';

/// Lưu lịch sử đo độ trễ (các dòng CSV) vào một file JSON trong thư mục dữ liệu riêng của app.
class LatencyHistory {
  LatencyHistory({this.baseDir});

  static const maxEntries = 50;

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  File get _file =>
      File('${(baseDir ?? appDataDir()).path}/latency_history.json');

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
    await writeFileAtomic(_file, jsonEncode(updated));
    return updated;
  }
}
