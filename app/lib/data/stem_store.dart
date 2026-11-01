import 'dart:convert';
import 'dart:io';

import '../engine/separation_client.dart';
import 'app_dirs.dart';

/// Stem giọng ca sĩ đã tách của từng bài (các đoạn FLAC server trả về), trong
/// `<dữ liệu app>/stems/<băm tên|cỡ>/`. Phần nhạc không lưu: engine tính bài − giọng khi nạp.
///
/// Khoá = tên file + kích thước như [SongPrefs]. Một bài chỉ được coi là đã tách khi có
/// `meta.json` (ghi sau cùng) và đủ các đoạn.
class StemStore {
  StemStore({this.baseDir});

  final Directory? baseDir;

  Directory dirFor({required String name, required int size}) => Directory(
    '${(baseDir ?? appDataDir()).path}/stems/${_hash('$name|$size')}',
  );

  /// Thư mục tạm cho một lần tách (xoá rồi tạo lại).
  Future<Directory> prepare({required String name, required int size}) async {
    final d = dirFor(name: name, size: size);
    if (await d.exists()) await d.delete(recursive: true);
    return d.create(recursive: true);
  }

  static String chunkName(int index) =>
      'vocals_${index.toString().padLeft(3, '0')}.flac';

  /// Ghi sau khi đã tải đủ [chunks] đoạn. [elapsed], [serverSeconds]: số đo của lần tách, để mở
  /// lại bài vẫn thấy "Tách xong sau … s".
  Future<void> markDone({
    required String name,
    required int size,
    required int chunks,
    Duration? elapsed,
    double? serverSeconds,
  }) async {
    final d = dirFor(name: name, size: size);
    await writeFileAtomic(
      File('${d.path}/meta.json'),
      jsonEncode({
        'name': name,
        'size': size,
        'chunks': chunks,
        'elapsed_ms': ?elapsed?.inMilliseconds,
        'server_seconds': ?serverSeconds,
      }),
    );
  }

  /// Các đoạn giọng của bài (đúng thứ tự), hoặc null nếu chưa tách / thiếu file.
  Future<List<String>?> vocalsFor({
    required String name,
    required int size,
  }) async {
    final d = dirFor(name: name, size: size);
    try {
      final meta = jsonDecode(await File('${d.path}/meta.json').readAsString());
      if (meta is! Map || meta['name'] != name || meta['size'] != size) {
        return null;
      }
      final n = meta['chunks'];
      if (n is! int || n <= 0) return null;
      final paths = [for (var i = 0; i < n; i++) '${d.path}/${chunkName(i)}'];
      for (final p in paths) {
        if (!await File(p).exists()) {
          return null;
        }
      }
      return paths;
    } catch (_) {
      return null;
    }
  }

  /// Lần tách đã lưu (các đoạn giọng + số đo), hoặc null nếu chưa tách / không có số đo
  /// (bản tách cũ).
  Future<SeparationResult?> resultFor({
    required String name,
    required int size,
  }) async {
    final vocals = await vocalsFor(name: name, size: size);
    if (vocals == null) return null;
    try {
      final d = dirFor(name: name, size: size);
      final meta =
          jsonDecode(await File('${d.path}/meta.json').readAsString()) as Map;
      final ms = meta['elapsed_ms'];
      if (ms is! int) return null;
      final server = meta['server_seconds'];
      return SeparationResult(
        vocals: [for (final p in vocals) File(p)],
        elapsed: Duration(milliseconds: ms),
        serverSeconds: server is num ? server.toDouble() : null,
      );
    } catch (_) {
      return null;
    }
  }

  Future<void> delete({required String name, required int size}) async {
    final d = dirFor(name: name, size: size);
    if (await d.exists()) await d.delete(recursive: true);
  }

  // FNV-1a 64 bit (không cần thư viện băm; chỉ để đặt tên thư mục). int của Dart 64 bit có dấu:
  // nhân tràn tự quấn mod 2^64, in hex theo hai nửa 32 bit.
  static String _hash(String s) {
    var h = 0xcbf29ce484222325;
    for (final b in utf8.encode(s)) {
      h ^= b;
      h *= 0x100000001b3;
    }
    String half(int v) => (v & 0xFFFFFFFF).toRadixString(16).padLeft(8, '0');
    return '${half(h >> 32)}${half(h)}';
  }
}
