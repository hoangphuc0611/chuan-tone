// Nhật ký thử nghiệm, chạy nền ngắn (M8 B7).

import 'dart:convert';
import 'dart:io';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/test_log.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/background.dart';
import 'package:chuantone/engine_info.dart';
import 'package:chuantone/pages/settings_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// Các dòng nhật ký đã ghi.
Future<List<Map<String, dynamic>>> readLog() async {
  await TestLog.flush();
  final f = TestLog.file;
  if (!f.existsSync()) return const [];
  return [
    for (final l in f.readAsLinesSync())
      if (l.isNotEmpty) jsonDecode(l) as Map<String, dynamic>,
  ];
}

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('test_log'));
  tearDown(() async {
    TestLog.reset();
    await LibraryDb.closeAll();
    dir.deleteSync(recursive: true);
  });

  group('nhật ký thử nghiệm', () {
    test('tắt (mặc định): không ghi gì', () async {
      await TestLog.init(baseDir: dir);
      expect(TestLog.enabled, isFalse);
      TestLog.screen('home');
      TestLog.event('play');
      expect(await readLog(), isEmpty);
      expect(await TestLog.size(), 0);
    });

    test('bật: mốc phiên + sự kiện có mốc thời gian; thanh trượt chỉ ghi lần đầu trong 0,5 s', () async {
      await AppSettings(baseDir: dir).setTestLog(true);
      var t = DateTime(2026, 11, 3, 9);
      TestLog.now = () => t;
      await TestLog.init(baseDir: dir, session: {'version': '1.0.0'});
      TestLog.screen('sing');
      TestLog.event('fxmix', {'value': 1.1}, true);
      t = t.add(const Duration(milliseconds: 200));
      TestLog.event('fxmix', {'value': 1.3}, true); // bỏ
      t = t.add(const Duration(milliseconds: 400));
      TestLog.event('fxmix', {'value': 1.5}, true);
      TestLog.error('Không mở được bài này.', details: 'tc_load_beat: X');
      final log = await readLog();
      expect(
        [for (final e in log) e['e']],
        ['session', 'screen', 'fxmix', 'fxmix', 'error'],
      );
      expect(log[0]['version'], '1.0.0');
      expect(log[0]['t'], '2026-11-03T09:00:00.000');
      expect(log[1]['name'], 'sing');
      expect([log[2]['value'], log[3]['value']], [1.1, 1.5]);
      expect(log[4]['details'], 'tc_load_beat: X');
      expect(await TestLog.size(), greaterThan(0));

      // Tắt: ghi mốc tắt rồi thôi; lần mở sau đọc lại cài đặt.
      await TestLog.setEnabled(false);
      TestLog.screen('home');
      expect((await readLog()).last['e'], 'log.off');
      expect(await AppSettings(baseDir: dir).testLog(), isFalse);
      await TestLog.clear();
      expect(await TestLog.size(), 0);
    });
  });

  group('chạy nền ngắn', () {
    test('xin trước, báo xong sau — kể cả khi việc chính lỗi', () async {
      final calls = <String>[];
      final r = await inBackground(
        'Xuất',
        () async {
          calls.add('việc');
          return 7;
        },
        begin: (name) async {
          calls.add('xin $name');
          return 3;
        },
        end: (id) async => calls.add('xong $id'),
      );
      expect(r, 7);
      expect(calls, ['xin Xuất', 'việc', 'xong 3']);

      calls.clear();
      await expectLater(
        inBackground<void>(
          'Tách',
          () async => throw StateError('lỗi'),
          begin: (_) async => 5,
          end: (id) async => calls.add('xong $id'),
        ),
        throwsStateError,
      );
      expect(calls, ['xong 5']);
    });

    test('không xin được / nền tảng không có: vẫn làm việc chính', () async {
      var ended = false;
      expect(
        await inBackground(
          'Xuất',
          () async => 'ok',
          begin: (_) async => throw StateError('kênh lỗi'),
          end: (_) async => ended = true,
        ),
        'ok',
      );
      expect(ended, isFalse);
      // Test không có plugin: kênh native trả null, không lỗi.
      expect(await inBackground('Xuất', () async => 1), 1);
    });
  });

  testWidgets('Cài đặt → Nâng cao: bật nhật ký, chia sẻ / xoá', (tester) async {
    tester.view.physicalSize = const Size(800, 3200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.runAsync(() => TestLog.init(baseDir: dir));
    await tester.pumpWidget(
      MaterialApp(
        home: SettingsPage(
          engineInfo: const EngineInfo(
            ok: true,
            version: '0.9.0',
            apiVersion: '0.9.0',
          ),
          library: SongLibrary(baseDir: dir),
          service: AudioService.test(() => throw StateError('không dùng')),
        ),
      ),
    );
    for (var i = 0; i < 20; i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    expect(find.text('Ghi nhật ký thử nghiệm'), findsOneWidget);
    expect(find.text('Chia sẻ nhật ký'), findsNothing);
    await tester.tap(find.text('Ghi nhật ký thử nghiệm'));
    bool written() =>
        TestLog.file.existsSync() && TestLog.file.lengthSync() > 0;
    for (var i = 0; i < 100 && !written(); i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    expect(TestLog.enabled, isTrue);
    expect(find.text('Chia sẻ nhật ký'), findsOneWidget);
    // Lệnh ghi chạy trong đồng hồ giả của test: đọc thẳng file (không chờ trong runAsync).
    final first = jsonDecode(TestLog.file.readAsLinesSync().first) as Map;
    expect(first['e'], 'session');
    expect(first['version'], appVersion);
    for (var i = 0; i < 10; i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    await tester.pumpWidget(const SizedBox());
    await tester.runAsync(LibraryDb.closeAll);
  });
}
