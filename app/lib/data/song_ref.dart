import 'dart:io';

/// Một bài người dùng chọn: đường dẫn file + khoá nhớ theo bài (tên file + kích thước).
class SongRef {
  const SongRef({required this.path, required this.name, required this.size});

  final String path;
  final String name;
  final int size;

  static Future<SongRef> open(String path, String name) async =>
      SongRef(path: path, name: name, size: await File(path).length());
}
