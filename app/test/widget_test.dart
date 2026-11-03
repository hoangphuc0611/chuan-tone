// Test giao diện không cần thư viện native: truyền EngineInfo giả vào app.

import 'dart:io';

import 'package:chuantone/app.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/engine_info.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('app'));
  tearDown(() => dir.deleteSync(recursive: true));

  testWidgets('Trang chủ: tên app, khẩu hiệu; engine OK chỉ hiện ở Cài đặt', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(800, 3200); // dựng cả trang Cài đặt
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(
      ChuanToneApp(
        engineInfo: const EngineInfo(
          ok: true,
          version: '0.1.0',
          apiVersion: '0.1.0',
          framesProcessed: 256,
        ),
        library: SongLibrary(baseDir: dir),
      ),
    );
    expect(find.text('ChuẩnTone'), findsOneWidget);
    expect(find.text('Bấm bài là hát'), findsOneWidget);
    expect(find.text('ToneCore v0.1.0 – engine OK'), findsNothing);
    await tester.tap(find.byTooltip('Cài đặt'));
    await tester.pumpAndSettle();
    expect(find.text('ToneCore v0.1.0 – engine OK'), findsOneWidget);
  });

  testWidgets('hiển thị lỗi khi engine hỏng', (tester) async {
    await tester.pumpWidget(
      ChuanToneApp(
        engineInfo: const EngineInfo(
          ok: false,
          error: 'Không tìm thấy libtonecore.so',
        ),
        library: SongLibrary(baseDir: dir),
      ),
    );
    expect(find.text('Không khởi động được engine'), findsOneWidget);
    expect(find.text('Không tìm thấy libtonecore.so'), findsOneWidget);
  });
}
