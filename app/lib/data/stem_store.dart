import 'dart:convert';
import 'dart:io';

import '../engine/separation_client.dart';
import 'app_dirs.dart';
import 'library_db.dart';

/// Stem giọng ca sĩ đã tách của từng bài (các đoạn FLAC server trả về), trong
/// `<dữ liệu app>/stems/<băm tên|cỡ>/`. Phần nhạc không lưu: engine tính bài − giọng khi nạp.
///
/// Khoá = tên file + kích thước như [SongPrefs]. Một bài chỉ được coi là đã tách khi có
/// `meta.json` (ghi sau cùng) và đủ các đoạn. Đang tách dở: `pending.json` ghi job trên máy chủ để
/// tiếp tục khi bị ngắt (M8 B7).
class StemStore {
  StemStore({this.baseDir});

  final Directory? baseDir;

  Directory dirFor({required String name, required int size}) => Directory(
    '${(baseDir ?? appDataDir()).path}/stems/${stableHash(songKey(name, size))}',
  );

  /// Thư mục tạm cho một lần tách (xoá rồi tạo lại).
  Future<Directory> prepare({required String name, required int size}) async {
    final d = dirFor(name: name, size: size);
    if (await d.exists()) await d.delete(recursive: true);
    return d.create(recursive: true);
  }

  static String chunkName(int index) =>
      'vocals_${index.toString().padLeft(3, '0')}.flac';

  /// Lưu lần tách đang dở (ngay khi máy chủ tạo job).
  Future<void> savePending(PendingSeparation p) async {
    final d = dirFor(name: p.name, size: p.size);
    await d.create(recursive: true);
    await writeFileAtomic(
      File('${d.path}/pending.json'),
      jsonEncode(p.toJson()),
    );
  }

  /// Lần tách đang dở của bài, null nếu không có. Quá [PendingSeparation.maxAge] (máy chủ đã xoá
  /// job) → bỏ luôn phần đã tải.
  Future<PendingSeparation?> pendingFor({
    required String name,
    required int size,
    DateTime? now,
  }) async {
    final p = _readPending(dirFor(name: name, size: size));
    if (p == null || p.name != name || p.size != size) return null;
    if (p.expired(now ?? DateTime.now())) {
      await delete(name: name, size: size);
      return null;
    }
    return p;
  }

  /// Mọi lần tách đang dở (mới nhất trước) — Trang chủ nhắc tiếp tục.
  Future<List<PendingSeparation>> pending({DateTime? now}) async {
    final root = Directory('${(baseDir ?? appDataDir()).path}/stems');
    if (!await root.exists()) return const [];
    final out = <PendingSeparation>[];
    await for (final d in root.list()) {
      if (d is! Directory) continue;
      final p = _readPending(d);
      if (p == null) continue;
      final live = await pendingFor(name: p.name, size: p.size, now: now);
      if (live != null) out.add(live);
    }
    out.sort((a, b) => b.startedAt.compareTo(a.startedAt));
    return out;
  }

  PendingSeparation? _readPending(Directory d) {
    try {
      final f = File('${d.path}/pending.json');
      if (!f.existsSync()) return null;
      return PendingSeparation.fromJson(jsonDecode(f.readAsStringSync()));
    } catch (_) {
      return null;
    }
  }

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
    final pending = File('${d.path}/pending.json');
    if (await pending.exists()) await pending.delete();
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
}

/// Lần tách đang dở của một bài (M8 B7): máy chủ, job, lúc bắt đầu. Máy chủ giữ job 24 giờ.
class PendingSeparation {
  const PendingSeparation({
    required this.name,
    required this.size,
    required this.server,
    required this.job,
    required this.startedAt,
  });

  static const maxAge = Duration(hours: 24);

  final String name;
  final int size;
  final String server;
  final SeparationJob job;
  final DateTime startedAt;

  bool expired(DateTime now) => now.difference(startedAt) >= maxAge;

  Map<String, Object?> toJson() => {
    'name': name,
    'size': size,
    'server': server,
    'job': job.id,
    'chunks': job.chunks,
    'started': startedAt.toIso8601String(),
  };

  /// Dữ liệu hỏng → [FormatException].
  static PendingSeparation fromJson(Object? j) {
    if (j is Map &&
        j['name'] is String &&
        j['size'] is int &&
        j['server'] is String &&
        j['job'] is String &&
        j['chunks'] is int) {
      final started = DateTime.tryParse('${j['started']}');
      if (started != null) {
        return PendingSeparation(
          name: j['name'] as String,
          size: j['size'] as int,
          server: j['server'] as String,
          job: SeparationJob(
            id: j['job'] as String,
            chunks: j['chunks'] as int,
          ),
          startedAt: started,
        );
      }
    }
    throw const FormatException('pending.json hỏng');
  }
}
