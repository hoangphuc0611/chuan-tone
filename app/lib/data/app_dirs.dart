import 'dart:convert';
import 'dart:io';

/// Thư mục dữ liệu bền vững của app, không cần plugin: thư mục tạm của Dart nằm trong vùng dữ liệu
/// của app (Android: `<data>/cache`, iOS: `<container>/tmp`), nên thư mục cha của nó là vùng dữ
/// liệu của app (Android: `<data>/files`, iOS: `<container>/Library/Application Support`).
Directory appDataDir() {
  final appRoot = Directory.systemTemp.parent;
  if (Platform.isIOS) {
    return Directory('${appRoot.path}/Library/Application Support');
  }
  if (Platform.isAndroid) return Directory('${appRoot.path}/files');
  return Directory.systemTemp;
}

/// Ghi file tạm rồi đổi tên: app bị tắt giữa chừng không làm hỏng file cũ.
Future<void> writeFileAtomic(File file, String contents) async {
  await file.parent.create(recursive: true);
  final tmp = File('${file.path}.tmp');
  await tmp.writeAsString(contents, flush: true);
  await tmp.rename(file.path);
}

/// Băm FNV-1a 64 bit ra 16 ký tự hex — đặt tên thư mục / file theo khoá bài (không cần thư viện
/// băm). int của Dart 64 bit có dấu: nhân tràn tự quấn mod 2^64, in hex theo hai nửa 32 bit.
String stableHash(String s) {
  var h = 0xcbf29ce484222325;
  for (final b in utf8.encode(s)) {
    h ^= b;
    h *= 0x100000001b3;
  }
  String half(int v) => (v & 0xFFFFFFFF).toRadixString(16).padLeft(8, '0');
  return '${half(h >> 32)}${half(h)}';
}
