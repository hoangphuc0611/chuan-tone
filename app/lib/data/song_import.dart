import 'dart:io';

import 'package:file_picker/file_picker.dart';

import 'song_library.dart';

/// Đuôi file lời nhận được.
const lyricsExtensions = ['lrc'];

bool isLyricsFile(String name) =>
    lyricsExtensions.contains(_extension(name)) || _extension(name) == 'txt';

bool isAudioFile(String name) =>
    SongLibrary.extensions.contains(_extension(name));

String _extension(String name) {
  final dot = name.lastIndexOf('.');
  return dot < 0 ? '' : name.substring(dot + 1).toLowerCase();
}

String _base(String name) {
  final dot = name.lastIndexOf('.');
  return foldVietnamese((dot > 0 ? name.substring(0, dot) : name).trim());
}

/// Hai tên file cùng tên (bỏ đuôi, bỏ dấu, không phân biệt hoa thường).
bool sameBaseName(String a, String b) => _base(a) == _base(b);

/// Ghép file lời với file nhạc chọn / gửi cùng lúc (docs/m8-plan.md điểm 5): cùng tên (bỏ đuôi, bỏ
/// dấu, không phân biệt hoa thường); không khớp tên mà đúng một nhạc + một lời thì ghép hai file đó.
/// Trả {tên nhạc: tên lời}.
Map<String, String> pairLyrics(
  List<String> audioNames,
  List<String> lyricNames,
) {
  final out = <String, String>{};
  final free = [...lyricNames];
  for (final a in audioNames) {
    final i = free.indexWhere((l) => sameBaseName(l, a));
    if (i >= 0) out[a] = free.removeAt(i);
  }
  if (out.isEmpty && audioNames.length == 1 && lyricNames.length == 1) {
    out[audioNames.single] = lyricNames.single;
  }
  return out;
}

/// Chọn file nhạc (app Tệp trên iOS — không cần quyền thư viện nhạc), có thể chọn kèm file lời
/// `.lrc`, rồi thêm vào thư viện; null nếu người dùng huỷ / không chọn file nhạc nào. Lời đọc không
/// được thì bỏ qua (vẫn thêm bài).
Future<LibrarySong?> pickSongIntoLibrary(SongLibrary library) async {
  final files = await FilePicker.pickFiles(
    type: FileType.custom,
    allowedExtensions: [...SongLibrary.extensions, ...lyricsExtensions],
  );
  final picked = [
    for (final f in files)
      if (f.path != null) (name: f.name, path: f.path!),
  ];
  final audio = [
    for (final f in picked)
      if (isAudioFile(f.name)) f,
  ];
  if (audio.isEmpty) return null;
  final lyrics = [
    for (final f in picked)
      if (isLyricsFile(f.name)) f,
  ];
  final pairs = pairLyrics(
    [for (final a in audio) a.name],
    [for (final l in lyrics) l.name],
  );
  LibrarySong? last;
  for (final a in audio) {
    // iOS / Android: bộ chọn file đưa bản chép trong thư mục tạm của app → chuyển hẳn vào thư viện.
    // Máy khác (chạy thử trên desktop): file gốc của người dùng → chép.
    final song = await library.add(
      a.path,
      name: a.name,
      move: Platform.isIOS || Platform.isAndroid,
    );
    final lrc = pairs[a.name];
    if (lrc != null) {
      try {
        await library.setLyrics(
          song.name,
          song.size,
          lyrics.firstWhere((l) => l.name == lrc).path,
        );
      } on Object {
        // file lời hỏng: bài vẫn thêm được
      }
    }
    last = song;
  }
  return last;
}

/// Chọn một file lời (`.lrc`, hoặc `.txt` có mốc thời gian); trả đường dẫn, null nếu huỷ.
Future<String?> pickLyricsFile() async {
  final file = await FilePicker.pickFile(
    type: FileType.custom,
    allowedExtensions: const ['lrc', 'txt'],
  );
  return file?.path;
}
