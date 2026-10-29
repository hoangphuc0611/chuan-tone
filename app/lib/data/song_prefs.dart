import 'dart:convert';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'app_dirs.dart';

/// Lựa chọn của người dùng theo từng bài — tone beat đã đổi (−6…+6) và preset hiệu ứng giọng — để
/// mở lại bài là hát ngay như lần trước. Kèm preset chọn gần nhất (dùng khi bài mới không gợi ý được
/// preset nào).
///
/// Khoá = tên file + kích thước, như [AnalysisCache] nhưng KHÔNG kèm phiên bản engine: đây là lựa
/// chọn của người dùng, không phải kết quả thuật toán.
class SongPrefs {
  SongPrefs({this.baseDir});

  static const maxEntries = 500;

  // Không trùng khoá bài được (khoá bài luôn có '|').
  static const _lastGenreKey = '*lastGenre';

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  // Các lần ghi chạy lần lượt: hai lần ghi gần nhau (đổi tone rồi đổi preset) không ghi đè nhau.
  Future<void> _writes = Future.value();

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

  Future<Map<String, dynamic>> _entry(String name, int size) async {
    final entry = (await _readAll())[_keyOf(name, size)];
    return entry is Map<String, dynamic> ? entry : const {};
  }

  /// Tone beat đã chọn cho bài (0 nếu chưa đổi).
  Future<int> transposeFor({required String name, required int size}) async {
    final t = (await _entry(name, size))['transpose'];
    return t is int && t >= -6 && t <= 6 ? t : 0;
  }

  Future<void> setTranspose({
    required String name,
    required int size,
    required int semitones,
  }) => _update(name, size, 'transpose', semitones == 0 ? null : semitones);

  /// Preset người dùng đã chọn cho bài (null nếu chưa chọn).
  Future<Genre?> genreFor({required String name, required int size}) async =>
      _genreByName((await _entry(name, size))['genre']);

  Future<void> setGenre({
    required String name,
    required int size,
    required Genre genre,
  }) => _update(name, size, 'genre', genre.name);

  /// Preset chọn gần nhất (bài nào cũng được).
  Future<Genre?> lastGenre() async =>
      _genreByName((await _readAll())[_lastGenreKey]);

  Future<void> setLastGenre(Genre genre) => _serial(() async {
    final all = await _readAll();
    all[_lastGenreKey] = genre.name;
    await writeFileAtomic(_file, jsonEncode(all));
  });

  Future<void> _serial(Future<void> Function() op) {
    final next = _writes.then((_) => op());
    _writes = next.catchError((_) {});
    return next;
  }

  static Genre? _genreByName(Object? name) {
    for (final g in Genre.values) {
      if (g.name == name) return g;
    }
    return null;
  }

  /// Ghi một trường của bài (null = xoá trường; bài hết trường thì xoá bài) và đưa bài xuống cuối
  /// (= mới nhất); quá [maxEntries] bài thì bỏ bài cũ nhất.
  Future<void> _update(String name, int size, String field, Object? value) =>
      _serial(() async {
        final all = await _readAll();
        final key = _keyOf(name, size);
        final old = all.remove(key);
        final entry = <String, dynamic>{
          if (old is Map<String, dynamic>) ...old,
        };
        if (value == null) {
          entry.remove(field);
        } else {
          entry[field] = value;
        }
        if (entry.isNotEmpty) all[key] = entry;
        var songs = all.keys.where((k) => k != _lastGenreKey).length;
        while (songs > maxEntries) {
          all.remove(all.keys.firstWhere((k) => k != _lastGenreKey));
          songs--;
        }
        await writeFileAtomic(_file, jsonEncode(all));
      });
}
