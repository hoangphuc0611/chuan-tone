import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'library_db.dart';

/// Lựa chọn của người dùng theo từng bài — tone beat đã đổi (−6…+6), preset hiệu ứng giọng, tone sửa
/// tay — để mở lại bài là hát ngay như lần trước. Kèm preset chọn gần nhất (dùng khi bài mới không
/// gợi ý được preset nào).
///
/// Lưu ở bảng `songs` của [LibraryDb] (M8; trước đó là `song_prefs.json`). Khoá = tên file + kích
/// thước, như [AnalysisCache] nhưng KHÔNG kèm phiên bản engine: đây là lựa chọn của người dùng,
/// không phải kết quả thuật toán.
class SongPrefs {
  SongPrefs({this.baseDir});

  /// Số bài tối đa chỉ có lựa chọn mà không có file trong thư viện (bài trong thư viện giữ hết).
  static const maxEntries = 500;

  /// Thư mục CSDL; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  Future<Map<String, Object?>> _row(String name, int size) async {
    final rows = await (await LibraryDb.open(baseDir))
        .query('songs', where: 'key = ?', whereArgs: [songKey(name, size)]);
    return rows.isEmpty ? const {} : rows.first;
  }

  /// Tone beat đã chọn cho bài (0 nếu chưa đổi).
  Future<int> transposeFor({required String name, required int size}) async {
    final t = (await _row(name, size))['transpose'];
    return t is int && t >= -6 && t <= 6 ? t : 0;
  }

  Future<void> setTranspose({
    required String name,
    required int size,
    required int semitones,
  }) => _update(name, size, {'transpose': semitones == 0 ? null : semitones});

  /// Preset người dùng đã chọn cho bài (null nếu chưa chọn).
  Future<Genre?> genreFor({required String name, required int size}) async =>
      _genreByName((await _row(name, size))['genre']);

  Future<void> setGenre({
    required String name,
    required int size,
    required Genre genre,
  }) => _update(name, size, {'genre': genre.name});

  /// Tone người dùng sửa tay cho bài (tone của bài gốc, chưa cộng đổi tone beat); null = theo kết
  /// quả dò.
  Future<MusicKey?> keyFor({required String name, required int size}) async {
    final row = await _row(name, size);
    final root = row['key_root'];
    MusicScale? scale;
    for (final s in MusicScale.values) {
      if (s.name == row['key_scale']) scale = s;
    }
    if (root is! int || root < 0 || root > 11 || scale == null) return null;
    return MusicKey(root, scale);
  }

  Future<void> setKey({
    required String name,
    required int size,
    required MusicKey? key,
  }) => _update(name, size, {
    'key_root': key?.root,
    'key_scale': key?.scale.name,
  });

  /// Preset chọn gần nhất (bài nào cũng được).
  Future<Genre?> lastGenre() async =>
      _genreByName(await LibraryDb.getSetting(baseDir, 'lastGenre'));

  Future<void> setLastGenre(Genre genre) =>
      LibraryDb.setSetting(baseDir, 'lastGenre', genre.name);

  static Genre? _genreByName(Object? name) {
    for (final g in Genre.values) {
      if (g.name == name) return g;
    }
    return null;
  }

  /// Ghi các trường của bài (null = xoá trường) và đánh dấu bài mới dùng. Bài không có file trong
  /// thư viện mà hết lựa chọn thì xoá hẳn; quá [maxEntries] bài như vậy thì bỏ bài cũ nhất.
  Future<void> _update(
    String name,
    int size,
    Map<String, Object?> values,
  ) async {
    final db = await LibraryDb.open(baseDir);
    final key = songKey(name, size);
    await db.transaction((txn) async {
      final t = LibraryDb.now();
      final n = await txn.update(
        'songs',
        {...values, 'last_used_at': t},
        where: 'key = ?',
        whereArgs: [key],
      );
      if (n == 0) {
        await txn.insert('songs', {
          'key': key,
          'name': name,
          'size': size,
          'title': titleFromFileName(name),
          'added_at': t,
          'last_used_at': t,
          ...values,
        });
      }
      await txn.delete(
        'songs',
        where:
            'key = ? AND file IS NULL AND transpose IS NULL AND genre IS NULL '
            'AND key_root IS NULL AND key_scale IS NULL',
        whereArgs: [key],
      );
      await txn.execute(
        'DELETE FROM songs WHERE file IS NULL AND key NOT IN '
        '(SELECT key FROM songs WHERE file IS NULL '
        'ORDER BY last_used_at DESC LIMIT ?)',
        [maxEntries],
      );
    });
  }
}
