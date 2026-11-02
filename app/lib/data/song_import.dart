import 'dart:io';

import 'package:file_picker/file_picker.dart';

import 'song_library.dart';

/// Chọn file nhạc (app Tệp trên iOS — không cần quyền thư viện nhạc) rồi thêm vào thư viện; null
/// nếu người dùng huỷ.
Future<LibrarySong?> pickSongIntoLibrary(SongLibrary library) async {
  final files = await FilePicker.pickFiles(
    type: FileType.custom,
    allowedExtensions: SongLibrary.extensions,
  );
  if (files.isEmpty) return null;
  final path = files.single.path;
  if (path == null) return null;
  // iOS / Android: bộ chọn file đưa bản chép trong thư mục tạm của app → chuyển hẳn vào thư viện.
  // Máy khác (chạy thử trên desktop): file gốc của người dùng → chép.
  return library.add(
    path,
    name: files.single.name,
    move: Platform.isIOS || Platform.isAndroid,
  );
}
