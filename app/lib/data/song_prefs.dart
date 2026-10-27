import 'dart:convert';
import 'dart:io';

import 'app_dirs.dart';

/// Lựa chọn của người dùng theo từng bài — hiện là tone beat đã đổi (−6…+6) — để mở lại bài là
/// hát ngay ở tone hợp giọng mình.
///
/// Khoá = tên file + kích thước, như [AnalysisCache] nhưng KHÔNG kèm phiên bản engine: đây là lựa
/// chọn của người dùng, không phải kết quả thuật toán.
class SongPrefs {
  SongPrefs({this.baseDir});

  static const maxEntries = 500;

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  File get _file => File('${(baseDir ?? appDataDir()).path}/song_prefs.json');

  String _keyOf(String name, int size) => '$name|$size';

  Future<Map<String, dynamic>> _readAll() async {
    try {
      final f = _file;
      if (!await f.exists()) return {};
      final data = jsonDecode(await f.readAsString());
      if (data is Map<String, dynamic>) return data;
    } catch (_) {
      // File hỏng → coi như chưa có gì.
    }
    return {};
  }

  /// Tone beat đã chọn cho bài (0 nếu chưa đổi).
  Future<int> transposeFor({required String name, required int size}) async {
    final entry = (await _readAll())[_keyOf(name, size)];
    if (entry is! Map<String, dynamic>) return 0;
    final t = entry['transpose'];
    return t is int && t >= -6 && t <= 6 ? t : 0;
  }

  Future<void> setTranspose({
    required String name,
    required int size,
    required int semitones,
  }) async {
    final all = await _readAll();
    final key = _keyOf(name, size);
    all.remove(key); // đưa xuống cuối = mới nhất
    if (semitones != 0) all[key] = {'transpose': semitones};
    while (all.length > maxEntries) {
      all.remove(all.keys.first);
    }
    await writeFileAtomic(_file, jsonEncode(all));
  }
}
