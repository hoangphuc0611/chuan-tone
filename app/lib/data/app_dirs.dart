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
