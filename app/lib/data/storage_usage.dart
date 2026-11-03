import 'dart:io';

import 'app_dirs.dart';
import 'song_library.dart';
import 'stem_store.dart';
import 'take_store.dart';

/// Dung lượng app dùng (Cài đặt → Bộ nhớ, M8 B4), byte.
class StorageUsage {
  const StorageUsage({
    this.songs = 0,
    this.stems = 0,
    this.takes = 0,
    this.exports = 0,
  });

  /// File nhạc + lời trong thư viện.
  final int songs;

  /// Giọng ca sĩ đã tách.
  final int stems;

  /// Bản thu (giọng khô, giọng đã xử lý, nhạc, video camera) — chưa gồm file đã xuất.
  final int takes;

  /// File âm thanh / video đã xuất (dựng lại được từ bản thu).
  final int exports;
}

/// File đã xuất trong thư mục bản thu (`xuat-*.m4a`, `video-*.mp4` — màn Nghe lại & chỉnh).
bool isExportFile(String name) =>
    (name.startsWith('xuat-') && name.endsWith('.m4a')) ||
    (name.startsWith('video-') && name.endsWith('.mp4'));

Future<int> _dirSize(Directory d, {bool Function(File f)? where}) async {
  var n = 0;
  if (!await d.exists()) return 0;
  await for (final e in d.list(recursive: true, followLinks: false)) {
    if (e is File && (where == null || where(e))) {
      try {
        n += await e.length();
      } catch (_) {}
    }
  }
  return n;
}

String _name(File f) => f.uri.pathSegments.last;

Future<StorageUsage> measureStorage({Directory? baseDir}) async {
  final base = baseDir ?? appDataDir();
  final takes = TakeStore(baseDir: baseDir).root;
  return StorageUsage(
    songs: await _dirSize(SongLibrary(baseDir: baseDir).dir),
    stems: await _dirSize(Directory('${base.path}/stems')),
    takes: await _dirSize(takes, where: (f) => !isExportFile(_name(f))),
    exports: await _dirSize(takes, where: (f) => isExportFile(_name(f))),
  );
}

/// Xoá mọi file đã xuất (bản thu giữ nguyên, xuất lại được).
Future<void> deleteExports({Directory? baseDir}) async {
  final root = TakeStore(baseDir: baseDir).root;
  if (!await root.exists()) return;
  await for (final e in root.list(recursive: true, followLinks: false)) {
    if (e is File && isExportFile(_name(e))) {
      try {
        await e.delete();
      } catch (_) {}
    }
  }
}

/// Xoá mọi bản thu (kể cả file đã xuất).
Future<void> deleteAllTakes({Directory? baseDir}) async {
  final root = TakeStore(baseDir: baseDir).root;
  if (await root.exists()) await root.delete(recursive: true);
}

/// Xoá mọi bản tách giọng (bài vẫn hát được; muốn "Giữ giọng ca sĩ gốc" thì tách lại).
Future<void> deleteAllStems({Directory? baseDir}) async {
  final d = StemStore(baseDir: baseDir).dirFor(name: '', size: 0).parent;
  if (await d.exists()) await d.delete(recursive: true);
}

/// Xoá mọi bài trong thư viện (file nhạc, lời, giọng đã tách, kết quả dò); bản thu giữ nguyên.
Future<void> deleteAllSongs(SongLibrary library) async {
  for (final s in await library.songs()) {
    await library.delete(s);
  }
}
