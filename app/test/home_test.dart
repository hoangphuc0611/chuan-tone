import 'dart:io';

import 'package:chuantone/app.dart';
import 'package:chuantone/data/analysis_cache.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/engine_info.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

const _ok = EngineInfo(
  ok: true,
  version: '9.9.9-test',
  apiVersion: '9.9.9',
  framesProcessed: 256,
);

SongAnalysis _analysis(MusicKey key) => SongAnalysis(
  duration: const Duration(minutes: 4),
  sampleRate: 44100,
  channels: 2,
  key: key,
  keyConfidence: 0.9,
  tuningCents: 0,
  bpm: 90,
  bpmConfidence: 0.8,
  integratedLufs: -12,
  samplePeakDb: -1,
  timeline: const [],
);

void main() {
  late Directory dir;
  late Directory outside;
  setUp(() {
    dir = Directory.systemTemp.createTempSync('home');
    outside = Directory.systemTemp.createTempSync('home_src');
  });
  tearDown(() {
    dir.deleteSync(recursive: true);
    outside.deleteSync(recursive: true);
  });

  /// Chạy tới khi [done] (IO thật ngoài đồng hồ giả của test).
  Future<void> settle(WidgetTester tester, bool Function() done) async {
    for (var i = 0; i < 200 && !done(); i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    expect(done(), isTrue, reason: 'hết giờ chờ');
  }

  /// Thư viện có 4 bài, thêm theo thứ tự A → D (D mới dùng nhất).
  Future<SongLibrary> library(WidgetTester tester) async {
    final lib = SongLibrary(baseDir: dir);
    await tester.runAsync(() async {
      for (final (i, n) in ['A', 'B', 'C', 'D'].indexed) {
        final f = File('${outside.path}/Bài $n.mp3')
          ..writeAsBytesSync(List.filled(10 + i, 1));
        await lib.add(f.path);
      }
      await SongPrefs(baseDir: dir)
          .setTranspose(name: 'Bài D.mp3', size: 13, semitones: 2);
      await AnalysisCache(engineVersion: _ok.version, baseDir: dir).store(
        name: 'Bài D.mp3',
        size: 13,
        analysis: _analysis(const MusicKey(7, MusicScale.naturalMinor)),
      );
      final lrc = File('${outside.path}/c.lrc')
        ..writeAsStringSync('[00:01.00]Lời\n');
      await lib.setLyrics('Bài C.mp3', 12, lrc.path);
    });
    return lib;
  }

  /// Cho các lần đọc CSDL còn dở (chạy trong đồng hồ giả) xong, gỡ màn rồi đóng CSDL — đóng khi
  /// còn lệnh dở thì chờ mãi.
  Future<void> closeDb(WidgetTester tester) async {
    for (var i = 0; i < 10; i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    await tester.pumpWidget(const SizedBox());
    await tester.runAsync(LibraryDb.closeAll);
  }

  testWidgets('Hát tiếp (3 bài mới nhất), tất cả bài, bài mẫu, bản thu', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(800, 1600); // đủ cao để dựng cả trang
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final lib = await library(tester);
    final sung = <String>[];
    await tester.pumpWidget(
      MaterialApp(
        home: HomePage(
          engineInfo: _ok,
          library: lib,
          onSing: (_, s) async => sung.add(s.title),
        ),
      ),
    );
    await settle(tester, () => find.text('Hát tiếp').evaluate().isNotEmpty);
    expect(find.text('ChuẩnTone'), findsOneWidget);
    expect(find.text('Thêm bài hát'), findsOneWidget);
    expect(find.text('Bài D'), findsOneWidget);
    expect(find.text('Bài C'), findsOneWidget);
    expect(find.text('Bài B'), findsOneWidget);
    expect(find.text('Bài A'), findsNothing); // chỉ 3 bài
    expect(find.text('Son thứ (Gm) · tone +2'), findsOneWidget);
    expect(find.text('Chưa dò tone · có lời'), findsOneWidget);
    expect(find.text('Tất cả bài hát (4)'), findsOneWidget);
    expect(find.text('Bài mẫu'), findsOneWidget);
    expect(find.text('Luyện giọng: Đô Rê Mi'), findsOneWidget);
    expect(find.text('Bản thu của tôi'), findsOneWidget);
    expect(find.text('ToneCore v9.9.9-test – engine OK'), findsNothing);

    await tester.tap(find.text('Bài D'));
    await tester.pump();
    expect(sung, ['Bài D']);
    await closeDb(tester);
  });

  testWidgets('bấm bài mẫu: chép vào thư viện kèm lời rồi vào hát', (
    tester,
  ) async {
    final lib = SongLibrary(baseDir: dir);
    final sung = <String>[];
    await tester.pumpWidget(
      MaterialApp(
        home: HomePage(
          engineInfo: _ok,
          library: lib,
          onSing: (_, s) async => sung.add('${s.title} ${s.hasLyrics}'),
        ),
      ),
    );
    await settle(tester, () => find.text('Bài mẫu').evaluate().isNotEmpty);
    expect(find.text('Hát tiếp'), findsNothing); // thư viện trống
    await tester.tap(find.text('Luyện giọng: La thứ'));
    await settle(tester, () => sung.isNotEmpty);
    expect(sung, ['Luyện giọng: La thứ true']);
    // Trang chủ cập nhật: bài mẫu vừa hát nằm ở "Hát tiếp".
    await settle(tester, () => find.text('Hát tiếp').evaluate().isNotEmpty);
    expect(find.text('Tất cả bài hát (1)'), findsOneWidget);
    await closeDb(tester);
  });

  testWidgets('⚙ mở Cài đặt: thông tin engine, Đo độ trễ, Phòng thử', (
    tester,
  ) async {
    final lib = SongLibrary(baseDir: dir);
    await tester.pumpWidget(
      MaterialApp(
        home: HomePage(engineInfo: _ok, library: lib),
      ),
    );
    await settle(tester, () => find.text('Bài mẫu').evaluate().isNotEmpty);
    await tester.tap(find.byTooltip('Cài đặt'));
    await tester.pumpAndSettle();
    expect(find.text('Cài đặt'), findsOneWidget);
    expect(find.text('ToneCore v9.9.9-test – engine OK'), findsOneWidget);
    expect(find.text('Đo độ trễ'), findsOneWidget);
    expect(find.text('Phòng thử'), findsOneWidget);
    await closeDb(tester);
  });

  testWidgets('engine lỗi: báo ngay đầu Trang chủ, khoá các nút', (
    tester,
  ) async {
    final lib = SongLibrary(baseDir: dir);
    await tester.pumpWidget(
      MaterialApp(
        home: HomePage(
          engineInfo: const EngineInfo(ok: false, error: 'Thiếu libtonecore'),
          library: lib,
        ),
      ),
    );
    await settle(tester, () => find.text('Bài mẫu').evaluate().isNotEmpty);
    expect(find.text('Không khởi động được engine'), findsOneWidget);
    expect(find.text('Thiếu libtonecore'), findsOneWidget);
    final add = tester.widget<FilledButton>(
      find.ancestor(
        of: find.text('Thêm bài hát'),
        matching: find.byType(FilledButton),
      ),
    );
    expect(add.onPressed, isNull);
    await closeDb(tester);
  });

  testWidgets('máy nhỏ 320 px, chữ 130%: không tràn', (tester) async {
    tester.view.physicalSize = const Size(640, 1136);
    tester.view.devicePixelRatio = 2;
    tester.platformDispatcher.textScaleFactorTestValue = 1.3;
    addTearDown(tester.view.reset);
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    final lib = await library(tester);
    await tester.pumpWidget(
      MaterialApp(
        home: HomePage(engineInfo: _ok, library: lib),
      ),
    );
    await settle(tester, () => find.text('Hát tiếp').evaluate().isNotEmpty);
    expect(tester.takeException(), isNull);
    await tester.dragUntilVisible(
      find.textContaining('Bản thu của tôi'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(tester.takeException(), isNull);
    expect(find.textContaining('Bản thu của tôi'), findsOneWidget);
    await closeDb(tester);
  });
}
