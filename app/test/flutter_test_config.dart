import 'dart:async';

import 'package:chuantone/data/library_db.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

/// Chạy trước mọi file test: CSDL của app ([LibraryDb]) mở bằng SQLite của máy qua FFI (không có
/// plugin sqflite khi chạy `flutter test`). Bản không dùng isolate: chạy được trong `testWidgets`.
Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  sqfliteFfiInit();
  LibraryDb.factory = databaseFactoryFfiNoIsolate;
  await testMain();
}
