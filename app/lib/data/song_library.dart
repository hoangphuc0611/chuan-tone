import 'dart:convert';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'analysis_cache.dart';
import 'app_dirs.dart';
import 'library_db.dart';
import 'song_ref.dart';
import 'stem_store.dart';

/// Một bài trong thư viện (bảng `songs` có file).
class LibrarySong {
  const LibrarySong({
    required this.name,
    required this.size,
    required this.title,
    required this.path,
    required this.addedAt,
    required this.lastUsedAt,
    this.singCount = 0,
    this.sample = false,
    this.transpose = 0,
    this.manualKey,
    this.analysis,
    this.missing = false,
  });

  /// Tên file gốc lúc thêm (một nửa khoá bài).
  final String name;
  final int size;

  /// Tên hiển thị (đổi được).
  final String title;

  /// Đường dẫn tuyệt đối của file trong thư viện.
  final String path;
  final DateTime addedAt;
  final DateTime lastUsedAt;
  final int singCount;
  final bool sample;

  /// Tone beat đã đổi (−6…+6).
  final int transpose;

  /// Tone sửa tay (tone của bài gốc), null = theo kết quả dò.
  final MusicKey? manualKey;

  /// Kết quả phân tích đã lưu cho phiên bản engine hiện tại (null = chưa phân tích).
  final SongAnalysis? analysis;

  /// File nhạc không còn trên máy (cần thêm lại).
  final bool missing;

  String get key => songKey(name, size);

  SongRef get ref => SongRef(path: path, name: name, size: size);

  /// Tone của bài gốc: sửa tay nếu có, không thì theo máy dò.
  MusicKey? get key0 => manualKey ?? analysis?.key;
}

/// Thư viện bài (M8 B1): file nhạc nằm trong `<dữ liệu app>/library/<băm khoá>.<đuôi>` — iOS không
/// tự xoá thư mục này như thư mục tạm của bộ chọn file — kèm một dòng trong bảng `songs`.
class SongLibrary {
  SongLibrary({this.baseDir, StemStore? stems})
    : _stems = stems ?? StemStore(baseDir: baseDir);

  /// Thư mục dữ liệu; null = thư mục của app (test truyền thư mục tạm).
  final Directory? baseDir;
  final StemStore _stems;

  /// Đuôi file nhạc nhận được (bộ chọn file, "Mở bằng").
  static const extensions = ['mp3', 'm4a', 'aac', 'wav', 'flac'];

  Directory get dir => Directory('${(baseDir ?? appDataDir()).path}/library');

  /// Thêm file [source] vào thư viện; [name] = tên file gốc (mặc định lấy từ đường dẫn).
  /// [move] = chuyển file đi (file tạm của bộ chọn file / "Mở bằng"), không thì chép.
  /// Bài đã có (cùng tên + kích thước, file còn) thì không chép lại, chỉ đánh dấu mới dùng.
  Future<LibrarySong> add(
    String source, {
    String? name,
    bool move = false,
  }) async {
    final src = File(source);
    final fileName = name ?? source.split(Platform.pathSeparator).last;
    final size = await src.length();
    final key = songKey(fileName, size);
    final db = await LibraryDb.open(baseDir);

    final old = await db.query('songs', where: 'key = ?', whereArgs: [key]);
    final oldFile = old.isEmpty ? null : old.first['file'] as String?;
    if (oldFile != null && await File('${dir.path}/$oldFile').exists()) {
      if (move && src.absolute.path != File('${dir.path}/$oldFile').path) {
        await _deleteQuietly(src);
      }
    } else {
      final rel = '${stableHash(key)}${_extensionOf(fileName)}';
      await dir.create(recursive: true);
      final dest = File('${dir.path}/$rel');
      if (move) {
        try {
          await src.rename(dest.path);
        } on FileSystemException {
          // Khác ổ đĩa → chép rồi xoá.
          await src.copy(dest.path);
          await _deleteQuietly(src);
        }
      } else {
        await src.copy(dest.path);
      }
      final t = LibraryDb.now();
      await db.transaction((txn) async {
        final n = await txn.update(
          'songs',
          {'file': rel},
          where: 'key = ?',
          whereArgs: [key],
        );
        if (n == 0) {
          await txn.insert('songs', {
            'key': key,
            'name': fileName,
            'size': size,
            'title': titleFromFileName(fileName),
            'file': rel,
            'added_at': t,
            'last_used_at': t,
          });
        }
      });
    }
    await touch(fileName, size, sung: false);
    return (await find(fileName, size))!;
  }

  /// Bài theo khoá; null nếu không có trong thư viện.
  Future<LibrarySong?> find(
    String name,
    int size, {
    String? engineVersion,
  }) async {
    final list = await _query(
      where: 'songs.key = ?',
      args: [songKey(name, size)],
      engineVersion: engineVersion,
    );
    return list.isEmpty ? null : list.first;
  }

  /// Các bài trong thư viện, mới dùng nhất trước. [query]: tìm theo tên, không phân biệt hoa
  /// thường / dấu tiếng Việt. [engineVersion]: kèm kết quả phân tích của phiên bản đó.
  Future<List<LibrarySong>> songs({
    String query = '',
    int? limit,
    String? engineVersion,
  }) async {
    final all = await _query(engineVersion: engineVersion);
    final hits = [
      for (final s in all)
        if (songMatches(s, query)) s,
    ];
    return limit == null || hits.length <= limit
        ? hits
        : hits.sublist(0, limit);
  }

  Future<List<LibrarySong>> _query({
    String? where,
    List<Object?> args = const [],
    String? engineVersion,
  }) async {
    final db = await LibraryDb.open(baseDir);
    final rows = await db.rawQuery(
      'SELECT songs.*, analysis.json AS analysis_json FROM songs '
      'LEFT JOIN analysis ON analysis.song_key = songs.key AND analysis.engine_version = ? '
      'WHERE songs.file IS NOT NULL${where == null ? '' : ' AND $where'} '
      'ORDER BY songs.last_used_at DESC',
      [engineVersion ?? '', ...args],
    );
    final out = <LibrarySong>[];
    for (final r in rows) {
      final path = '${dir.path}/${r['file']}';
      out.add(
        LibrarySong(
          name: r['name']! as String,
          size: r['size']! as int,
          title: r['title']! as String,
          path: path,
          addedAt: DateTime.fromMillisecondsSinceEpoch(r['added_at']! as int),
          lastUsedAt: DateTime.fromMillisecondsSinceEpoch(
            r['last_used_at']! as int,
          ),
          singCount: r['sing_count']! as int,
          sample: r['sample'] == 1,
          transpose: switch (r['transpose']) {
            final int t when t >= -6 && t <= 6 => t,
            _ => 0,
          },
          manualKey: _keyOf(r['key_root'], r['key_scale']),
          analysis: _analysisOf(r['analysis_json']),
          missing: !await File(path).exists(),
        ),
      );
    }
    return out;
  }

  /// Đánh dấu bài vừa mở (lên đầu "Hát tiếp"); [sung] = mở để hát (tăng số lần hát).
  Future<void> touch(String name, int size, {bool sung = true}) async {
    final db = await LibraryDb.open(baseDir);
    await db.rawUpdate(
      'UPDATE songs SET last_used_at = ?, sing_count = sing_count + ? WHERE key = ?',
      [LibraryDb.now(), sung ? 1 : 0, songKey(name, size)],
    );
  }

  /// Đổi tên hiển thị (rỗng → giữ tên cũ).
  Future<void> rename(LibrarySong song, String title) async {
    final t = title.trim();
    if (t.isEmpty) return;
    await (await LibraryDb.open(baseDir))
        .update('songs', {'title': t}, where: 'key = ?', whereArgs: [song.key]);
  }

  /// Xoá bài: file nhạc, giọng đã tách, kết quả phân tích, lựa chọn theo bài. Bản thu giữ nguyên.
  Future<void> delete(LibrarySong song) async {
    await _deleteQuietly(File(song.path));
    await _stems.delete(name: song.name, size: song.size);
    await AnalysisCache(
      engineVersion: '',
      baseDir: baseDir,
    ).remove(name: song.name, size: song.size);
    await (await LibraryDb.open(baseDir))
        .delete('songs', where: 'key = ?', whereArgs: [song.key]);
  }

  /// Tổng dung lượng file nhạc trong thư viện (byte).
  Future<int> totalBytes() async {
    var n = 0;
    if (!await dir.exists()) return 0;
    await for (final e in dir.list()) {
      if (e is File) n += await e.length();
    }
    return n;
  }

  static Future<void> _deleteQuietly(File f) async {
    try {
      if (await f.exists()) await f.delete();
    } catch (_) {}
  }

  static String _extensionOf(String fileName) {
    final dot = fileName.lastIndexOf('.');
    if (dot <= 0 || dot == fileName.length - 1) return '';
    final ext = fileName.substring(dot + 1).toLowerCase();
    return ext.length <= 5 && RegExp(r'^[a-z0-9]+$').hasMatch(ext)
        ? '.$ext'
        : '';
  }

  static MusicKey? _keyOf(Object? root, Object? scale) {
    if (root is! int || root < 0 || root > 11) return null;
    for (final s in MusicScale.values) {
      if (s.name == scale) return MusicKey(root, s);
    }
    return null;
  }

  static SongAnalysis? _analysisOf(Object? json) {
    if (json is! String) return null;
    try {
      return analysisFromJson(jsonDecode(json) as Map<String, dynamic>);
    } catch (_) {
      return null;
    }
  }
}

/// Bài có khớp chuỗi tìm [query] không (theo tên hiển thị hoặc tên file, bỏ dấu, không phân biệt
/// hoa thường); chuỗi rỗng khớp mọi bài.
bool songMatches(LibrarySong s, String query) {
  final q = foldVietnamese(query.trim());
  return q.isEmpty ||
      foldVietnamese(s.title).contains(q) ||
      foldVietnamese(s.name).contains(q);
}

/// Chữ thường, bỏ dấu tiếng Việt ("Duyên Phận" → "duyen phan") để tìm bài. Nhận cả chữ dựng sẵn
/// lẫn chữ tổ hợp (tên file trên iOS / macOS hay ở dạng chữ cái + dấu rời, U+0300…U+036F).
String foldVietnamese(String s) {
  final sb = StringBuffer();
  for (final r in s.toLowerCase().runes) {
    if (r >= 0x0300 && r <= 0x036F) continue;
    final c = String.fromCharCode(r);
    sb.write(_fold[c] ?? c);
  }
  return sb.toString();
}

final Map<String, String> _fold = () {
  const groups = {
    'a': 'àáạảãâầấậẩẫăằắặẳẵ',
    'e': 'èéẹẻẽêềếệểễ',
    'i': 'ìíịỉĩ',
    'o': 'òóọỏõôồốộổỗơờớợởỡ',
    'u': 'ùúụủũưừứựửữ',
    'y': 'ỳýỵỷỹ',
    'd': 'đ',
  };
  return {
    for (final MapEntry(:key, :value) in groups.entries)
      for (final c in value.split('')) c: key,
  };
}();
