import 'dart:convert';
import 'dart:io';

import 'package:sqflite/sqflite.dart';

import 'app_dirs.dart';

/// Khoá bài dùng chung cho mọi kho (thư viện, lựa chọn, phân tích, giọng đã tách, bản thu): tên
/// file + kích thước.
String songKey(String name, int size) => '$name|$size';

/// Tách khoá bài thành tên + kích thước (kích thước là phần sau dấu `|` cuối); null nếu sai dạng.
({String name, int size})? splitSongKey(String key) {
  final i = key.lastIndexOf('|');
  if (i <= 0) return null;
  final size = int.tryParse(key.substring(i + 1));
  if (size == null || size < 0) return null;
  return (name: key.substring(0, i), size: size);
}

/// Tên hiển thị mặc định của bài: tên file bỏ đuôi, `_` thành khoảng trắng.
String titleFromFileName(String name) {
  final dot = name.lastIndexOf('.');
  final base = dot > 0 ? name.substring(0, dot) : name;
  final t = base.replaceAll('_', ' ').replaceAll(RegExp(r'\s+'), ' ').trim();
  return t.isEmpty ? name : t;
}

/// CSDL SQLite của app (M8, docs/m8-plan.md mục 2), file `chuantone.db` trong thư mục dữ liệu:
/// - `songs`: thư viện bài + lựa chọn theo bài (tone beat, thể loại, tone sửa tay). Bài chỉ có lựa
///   chọn mà chưa có file trong thư viện (bài chọn trước M8) có `file` = null;
/// - `analysis`: kết quả phân tích theo bài + phiên bản engine;
/// - `settings`: cài đặt chung (giá trị là JSON).
///
/// Dùng SQLite của hệ điều hành qua sqflite (Android 9 có SQLite 3.22: không dùng UPSERT /
/// RETURNING). Lần tạo đầu chuyển dữ liệu JSON của M5–M7 vào rồi đổi tên file cũ thành
/// `.migrated` (không xoá).
abstract final class LibraryDb {
  static const fileName = 'chuantone.db';
  static const schemaVersion = 1;

  /// Nhà máy mở CSDL; null = plugin sqflite. Test đặt `databaseFactoryFfiNoIsolate`
  /// (test/flutter_test_config.dart).
  static DatabaseFactory? factory;

  static final _open = <String, Future<Database>>{};
  static int _lastNow = 0;

  /// Mốc thời gian (ms) tăng dần ngặt: hai lần ghi trong cùng 1 ms vẫn giữ đúng thứ tự cũ → mới.
  static int now() {
    final t = DateTime.now().millisecondsSinceEpoch;
    _lastNow = t > _lastNow ? t : _lastNow + 1;
    return _lastNow;
  }

  /// CSDL trong [baseDir] (null = thư mục dữ liệu của app); mở một lần cho mỗi đường dẫn.
  static Future<Database> open([Directory? baseDir]) {
    final dir = baseDir ?? appDataDir();
    final path = '${dir.path}/$fileName';
    return _open[path] ??= _openAt(dir, path).catchError((Object e) {
      _open.remove(path);
      throw e;
    });
  }

  /// Đóng mọi CSDL đã mở (test).
  static Future<void> closeAll() async {
    final all = _open.values.toList();
    _open.clear();
    for (final f in all) {
      try {
        await (await f).close();
      } catch (_) {}
    }
  }

  static Future<Database> _openAt(Directory dir, String path) async {
    await dir.create(recursive: true);
    final migrated = <File>[];
    final db = await (factory ?? databaseFactory).openDatabase(
      path,
      options: OpenDatabaseOptions(
        version: schemaVersion,
        onCreate: (db, version) async {
          await _createSchema(db);
          migrated.addAll(await _migrateJson(db, dir));
        },
      ),
    );
    for (final f in migrated) {
      try {
        await f.rename('${f.path}.migrated');
      } catch (_) {}
    }
    return db;
  }

  static Future<void> _createSchema(DatabaseExecutor db) async {
    await db.execute('''
      CREATE TABLE songs(
        key TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        size INTEGER NOT NULL,
        title TEXT NOT NULL,
        file TEXT,
        added_at INTEGER NOT NULL,
        last_used_at INTEGER NOT NULL,
        sing_count INTEGER NOT NULL DEFAULT 0,
        sample INTEGER NOT NULL DEFAULT 0,
        lrc_file TEXT,
        lrc_offset_ms INTEGER NOT NULL DEFAULT 0,
        transpose INTEGER,
        genre TEXT,
        key_root INTEGER,
        key_scale TEXT
      )''');
    await db.execute('CREATE INDEX songs_recent ON songs(last_used_at DESC)');
    await db.execute('''
      CREATE TABLE analysis(
        song_key TEXT NOT NULL,
        engine_version TEXT NOT NULL,
        json TEXT NOT NULL,
        stored_at INTEGER NOT NULL,
        PRIMARY KEY(song_key, engine_version)
      )''');
    await db.execute(
      'CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT NOT NULL)',
    );
  }

  /// Chuyển `song_prefs.json`, `analysis_cache.json`, `settings.json` (M5–M7) vào CSDL. File
  /// hỏng / giá trị sai kiểu thì bỏ qua phần đó. Trả các file đã đọc (để đổi tên sau khi mở xong).
  static Future<List<File>> _migrateJson(Database db, Directory dir) async {
    final done = <File>[];
    final base = now();
    final batch = db.batch();

    // {"tên|cỡ": {"transpose": 2, "genre": "bolero", "key": {"root": 7, "scale": "major"}},
    //  "*lastGenre": "ballad"}; thứ tự trong file = cũ → mới.
    final prefsFile = File('${dir.path}/song_prefs.json');
    final prefs = await _readJson(prefsFile);
    if (prefs != null) {
      done.add(prefsFile);
      var i = 0;
      for (final MapEntry(:key, :value) in prefs.entries) {
        if (key == '*lastGenre') {
          if (value is String) {
            batch.insert('settings', {
              'key': 'lastGenre',
              'value': jsonEncode(value),
            }, conflictAlgorithm: ConflictAlgorithm.replace);
          }
          continue;
        }
        final s = splitSongKey(key);
        if (s == null || value is! Map) continue;
        final k = value['key'];
        final t = base - prefs.length + i++;
        batch.insert('songs', {
          'key': key,
          'name': s.name,
          'size': s.size,
          'title': titleFromFileName(s.name),
          'added_at': t,
          'last_used_at': t,
          'transpose': ?_intOrNull(value['transpose']),
          'genre': ?_stringOrNull(value['genre']),
          if (k is Map) 'key_root': ?_intOrNull(k['root']),
          if (k is Map) 'key_scale': ?_stringOrNull(k['scale']),
        }, conflictAlgorithm: ConflictAlgorithm.ignore);
      }
    }

    // {"phiên bản engine|tên|cỡ": {kết quả}}; thứ tự = cũ → mới.
    final cacheFile = File('${dir.path}/analysis_cache.json');
    final cache = await _readJson(cacheFile);
    if (cache != null) {
      done.add(cacheFile);
      var i = 0;
      for (final MapEntry(:key, :value) in cache.entries) {
        final bar = key.indexOf('|');
        if (bar <= 0 ||
            value is! Map ||
            splitSongKey(key.substring(bar + 1)) == null) {
          continue;
        }
        batch.insert('analysis', {
          'song_key': key.substring(bar + 1),
          'engine_version': key.substring(0, bar),
          'json': jsonEncode(value),
          'stored_at': base - cache.length + i++,
        }, conflictAlgorithm: ConflictAlgorithm.replace);
      }
    }

    // {"separationServer": "...", "separationConsent": true, "deviceId": "..."}
    final settingsFile = File('${dir.path}/settings.json');
    final settings = await _readJson(settingsFile);
    if (settings != null) {
      done.add(settingsFile);
      for (final MapEntry(:key, :value) in settings.entries) {
        if (value == null) continue;
        batch.insert('settings', {
          'key': key,
          'value': jsonEncode(value),
        }, conflictAlgorithm: ConflictAlgorithm.replace);
      }
    }

    await batch.commit(noResult: true);
    return done;
  }

  /// Đọc file JSON dạng object; không có file → null; file hỏng → {} (vẫn đổi tên cho khỏi đọc
  /// lại lần sau).
  static Future<Map<String, dynamic>?> _readJson(File f) async {
    if (!await f.exists()) return null;
    try {
      final data = jsonDecode(await f.readAsString());
      return data is Map<String, dynamic> ? data : {};
    } catch (_) {
      return {};
    }
  }

  static int? _intOrNull(Object? v) => v is int ? v : null;
  static String? _stringOrNull(Object? v) => v is String ? v : null;

  // ---- settings ----

  /// Giá trị cài đặt [key] (đã giải JSON), null nếu chưa có / hỏng.
  static Future<Object?> getSetting(Directory? baseDir, String key) async {
    final rows = await (await open(baseDir)).query(
      'settings',
      columns: ['value'],
      where: 'key = ?',
      whereArgs: [key],
    );
    if (rows.isEmpty) return null;
    try {
      return jsonDecode(rows.first['value']! as String);
    } catch (_) {
      return null;
    }
  }

  /// Ghi cài đặt [key]; null = xoá.
  static Future<void> setSetting(
    Directory? baseDir,
    String key,
    Object? value,
  ) async {
    final db = await open(baseDir);
    if (value == null) {
      await db.delete('settings', where: 'key = ?', whereArgs: [key]);
    } else {
      await db.insert('settings', {
        'key': key,
        'value': jsonEncode(value),
      }, conflictAlgorithm: ConflictAlgorithm.replace);
    }
  }
}
