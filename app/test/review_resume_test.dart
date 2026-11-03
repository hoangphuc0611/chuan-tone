// Xuất bị ngắt (M8 B7) với engine thật, backend NULL: app bị đóng giữa lúc xuất → mở lại bản thu thì
// bỏ file dở, tự xuất lại (có báo); nhật ký thử nghiệm ghi lần xuất lại.
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/review_resume_test.dart

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/latency_history.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_ref.dart';
import 'package:chuantone/data/take_store.dart';
import 'package:chuantone/data/test_log.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/takes.dart';
import 'package:chuantone/pages/review_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('review_resume'));
  tearDown(() {
    TestLog.reset();
    dir.deleteSync(recursive: true);
  });

  /// Các sự kiện nhật ký đã ghi (đọc thẳng file).
  List<Map<String, dynamic>> log() => TestLog.file.existsSync()
      ? [
          for (final l in TestLog.file.readAsLinesSync())
            if (l.isNotEmpty) jsonDecode(l) as Map<String, dynamic>,
        ]
      : const [];

  testWidgets(
    'mở lại bản thu sau khi app bị đóng lúc đang xuất: bỏ file dở, tự xuất lại, có báo',
    (tester) async {
      tester.view.physicalSize = const Size(800, 2400);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final service = AudioService.test(
        () => ToneCoreEngine.create(maxBlockFrames: 256),
      );
      final take = await tester.runAsync(() async {
        await AppSettings(baseDir: dir).setTestLog(true);
        await TestLog.init(baseDir: dir);
        await service.ensureStarted();
        final e = service.engine!;
        final beatPath = '${dir.path}/bai.wav';
        writeCadenceWav(beatPath);
        await e.loadBeatAndWait(beatPath);
        final session = TakeSession(
          store: TakeStore(baseDir: dir),
          latency: LatencyHistory(baseDir: dir),
        );
        await session.start(
          e,
          TakeSnapshot.of(service, e, voiceGain: 1, beatGain: 0.8),
        );
        e.play();
        for (var b = 0; b < 2 * 48000 ~/ 256; b++) {
          e.processOffline(sineBlock(256, offset: b * 256), 256);
        }
        final t = await session.stop(
          e,
          SongRef(path: beatPath, name: 'bai.wav', size: 1),
        );
        // App bị đóng giữa lúc xuất: còn dấu + file M4A dở.
        await t.beginExport(ExportKind.audio);
        File('${t.dir.path}/xuat-100000.m4a').writeAsBytesSync(Uint8List(10));
        return t;
      });

      await tester.pumpWidget(
        MaterialApp(
          home: ReviewPage(
            take: take!,
            store: TakeStore(baseDir: dir),
            service: service,
          ),
        ),
      );
      var sawNotice = false;
      bool finished() =>
          log().any((e) => e['e'] == 'export.done' || e['e'] == 'export.fail');
      for (var i = 0; i < 600 && !finished(); i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 10)),
        );
        await tester.pump(const Duration(milliseconds: 20));
        sawNotice |= find
            .byKey(const ValueKey('review-export-notice'))
            .evaluate()
            .isNotEmpty;
      }
      expect(finished(), isTrue, reason: 'hết giờ chờ xuất lại');
      expect(sawNotice, isTrue); // "Lần xuất trước bị ngắt… đang xuất lại"
      expect(File('${take.dir.path}/xuat-100000.m4a').existsSync(), isFalse);
      expect(File('${take.dir.path}/export.pending').existsSync(), isFalse);
      final events = log();
      final start = events.firstWhere((e) => e['e'] == 'export.start');
      expect(start['kind'], 'audio');
      expect(start['retry'], isTrue);
      expect(
        events.any((e) => e['e'] == 'screen' && e['name'] == 'review'),
        isTrue,
      );
      // Máy có bộ nén AAC (macOS) thì có file mới; Linux (CI) không có AAC → báo lỗi xuất.
      if (events.any((e) => e['e'] == 'export.done')) {
        final out = take.dir.listSync().whereType<File>().where(
          (f) => f.path.endsWith('.m4a'),
        );
        expect(out, hasLength(1));
      }

      for (var i = 0; i < 10; i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 10)),
        );
        await tester.pump(const Duration(milliseconds: 20));
      }
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(service.shutdown);
      await tester.runAsync(LibraryDb.closeAll);
    },
    skip: skip != null,
  );
}
