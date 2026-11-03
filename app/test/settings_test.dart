import 'dart:io';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/storage_usage.dart';
import 'package:chuantone/data/take_store.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/latency_measure.dart';
import 'package:chuantone/engine_info.dart';
import 'package:chuantone/pages/latency_wizard_page.dart';
import 'package:chuantone/pages/settings_page.dart';
import 'package:chuantone/widgets/soundcard_prompt.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

const _ok = EngineInfo(
  ok: true,
  version: '0.9.0',
  apiVersion: '0.9.0',
  framesProcessed: 256,
);

LatencyResult _latency(double ms, {bool valid = true}) => LatencyResult(
  valid: valid,
  latencyMs: ms,
  latencyFrames: ms * 48,
  confidence: 0.9,
  validBursts: 5,
  totalBursts: 6,
  spreadMs: 0.2,
  reportedInputLatencyMs: 5,
  reportedOutputLatencyMs: 8,
  bufferFrames: 256,
  burstFrames: 128,
  route: AudioRoute.speaker,
);

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('settings'));
  tearDown(() => dir.deleteSync(recursive: true));

  group('cài đặt mới (B4)', () {
    test('micro, tự nghe lại giọng, xuất: mặc định và lưu lại', () async {
      final s = AppSettings(baseDir: dir);
      expect(await s.inputPreference(), InputKind.auto);
      expect(await s.autoMonitor(), isTrue);
      expect(await s.exportPrefs(), const ExportPrefs());
      await s.setInputPreference(InputKind.usb);
      await s.setAutoMonitor(false);
      await s.setExportPrefs(
        const ExportPrefs(aacBitrate: 256000, hd720: true, normalize: false),
      );
      expect(await s.inputPreference(), InputKind.usb);
      expect(await s.autoMonitor(), isFalse);
      expect(
        await s.exportPrefs(),
        const ExportPrefs(aacBitrate: 256000, hd720: true, normalize: false),
      );
      // Giá trị lạ trong CSDL → mặc định.
      await LibraryDb.setSetting(
        dir,
        'inputPreference',
        4,
      ); // Bluetooth: không chọn được
      await LibraryDb.setSetting(dir, 'export', {'aacBitrate': 1});
      expect(await s.inputPreference(), InputKind.auto);
      expect((await s.exportPrefs()).aacBitrate, 192000);
      await s.setInputPreference(InputKind.auto);
      expect(await LibraryDb.getSetting(dir, 'inputPreference'), isNull);
    });

    test('chế độ soundcard: mặc định chưa hỏi, lưu lại', () async {
      final s = AppSettings(baseDir: dir);
      expect(await s.soundcardMode(), isNull);
      expect(await s.soundcardFx(), SoundcardFx.soundcard);
      await s.setSoundcardMode(false);
      expect(await s.soundcardMode(), isFalse);
      await s.setSoundcardMode(true);
      await s.setSoundcardFx(SoundcardFx.app);
      expect(await s.soundcardMode(), isTrue);
      expect(await s.soundcardFx(), SoundcardFx.app);
      await s.setSoundcardFx(SoundcardFx.soundcard);
      expect(await LibraryDb.getSetting(dir, 'soundcardFx'), isNull);
    });

    test('mẹo: hiện một lần, đặt lại được', () async {
      final s = AppSettings(baseDir: dir);
      expect(await s.firstTimeTip('sing'), isTrue);
      expect(await s.firstTimeTip('sing'), isFalse);
      await s.resetTip('sing');
      expect(await s.firstTimeTip('sing'), isTrue);
    });
  });

  group('bộ nhớ', () {
    test(
      'đo từng nhóm, xoá file đã xuất / bản tách / bản thu / bài hát',
      () async {
        final lib = SongLibrary(baseDir: dir);
        final src = File('${dir.path}/src.mp3')
          ..writeAsBytesSync(List.filled(1000, 1));
        await lib.add(src.path, move: true);
        Directory('${dir.path}/stems/abc').createSync(recursive: true);
        File('${dir.path}/stems/abc/vocals_000.flac')
            .writeAsBytesSync(List.filled(300, 2));
        final take = await TakeStore(baseDir: dir).newTakeDir();
        File('${take.path}/dry.wav').writeAsBytesSync(List.filled(500, 3));
        File('${take.path}/xuat-101010.m4a')
            .writeAsBytesSync(List.filled(70, 4));
        File('${take.path}/video-101010.mp4')
            .writeAsBytesSync(List.filled(30, 5));

        var u = await measureStorage(baseDir: dir);
        expect(u.songs, 1000);
        expect(u.stems, 300);
        expect(u.takes, 500);
        expect(u.exports, 100);

        await deleteExports(baseDir: dir);
        u = await measureStorage(baseDir: dir);
        expect(u.exports, 0);
        expect(u.takes, 500);

        await deleteAllStems(baseDir: dir);
        expect((await measureStorage(baseDir: dir)).stems, 0);

        await deleteAllSongs(lib);
        expect((await measureStorage(baseDir: dir)).songs, 0);
        expect(await lib.songs(), isEmpty);

        await deleteAllTakes(baseDir: dir);
        expect((await measureStorage(baseDir: dir)).takes, 0);
        expect(isExportFile('xuat-1.m4a'), isTrue);
        expect(isExportFile('camera.mp4'), isFalse);
      },
    );
  });

  group('độ trễ', () {
    test('đánh giá bằng lời theo ngưỡng', () {
      expect(latencyVerdict(18).title, 'Máy bạn hát mượt');
      expect(latencyVerdict(35).level, LatencyLevel.smooth);
      expect(latencyVerdict(36).title, 'Hơi trễ, nên dùng tai nghe có dây');
      expect(latencyVerdict(60).level, LatencyLevel.slight);
      expect(latencyVerdict(61).level, LatencyLevel.high);
    });

    test('dòng CSV khớp tiêu đề (tools/latency/aggregate.py)', () {
      const m = EngineMetrics(
        state: EngineState.running,
        sampleRate: 48000,
        framesProcessed: 0,
        callbacks: 0,
        xruns: 0,
        cpuLoad: 0,
        bufferFrames: 256,
        burstFrames: 128,
        reportedInputLatencyMs: 5,
        reportedOutputLatencyMs: 8,
        route: AudioRoute.speaker,
        audioApi: AudioApi.remoteio,
        monitorActive: false,
        inputChannels: 1,
        cpuLoadPeak: 0,
        algorithmLatencyFrames: 48,
      );
      final line = latencyCsv(
        _latency(21.5),
        m,
        const DeviceLabel(brand: 'Apple', model: 'iPhone14,2', os: 'iOS 18'),
        now: DateTime(2026, 11, 3, 9),
      );
      // Tên máy có dấu phẩy nằm trong ngoặc kép: bỏ phần trong ngoặc rồi đếm cột.
      final unquoted = line.replaceAll(RegExp(r'"[^"]*"'), 'x');
      expect(unquoted.split(',').length, latencyCsvHeader.split(',').length);
      expect(line, contains('"iPhone14,2"'));
      expect(line, contains(',speaker,remoteio,48000,128,256,21.50,13.00,'));
    });

    testWidgets('trình đo từng bước: rút tai nghe → đo → kết quả bằng lời', (
      tester,
    ) async {
      var calls = 0;
      var next = _latency(0, valid: false);
      await tester.pumpWidget(
        MaterialApp(
          home: LatencyWizardPage(
            measure: () async {
              calls++;
              return next;
            },
          ),
        ),
      );
      await tester.pump();
      expect(find.text('Bước 1: Rút tai nghe'), findsOneWidget);
      await tester.tap(find.text('Tiếp'));
      await tester.pump();
      expect(find.text('Bước 2: Đo qua loa'), findsOneWidget);
      await tester.tap(find.text('Bắt đầu đo'));
      await tester.pump();
      expect(find.text('Chưa đo được'), findsOneWidget);
      await tester.tap(find.text('Đo lại'));
      await tester.pump();
      next = _latency(48);
      await tester.tap(find.text('Bắt đầu đo'));
      await tester.pump();
      expect(calls, 2);
      expect(find.text('Hơi trễ, nên dùng tai nghe có dây'), findsOneWidget);
      expect(find.textContaining('48 ms'), findsOneWidget);
    });

    testWidgets('chế độ soundcard: đo qua Loopback của soundcard (B6)', (
      tester,
    ) async {
      final service = AudioService.test(() => throw StateError('không dùng'))
        ..soundcardMode = true
        ..route = AudioRoute.usb;
      var next = _latency(0, valid: false);
      await tester.pumpWidget(
        MaterialApp(
          home: LatencyWizardPage(service: service, measure: () async => next),
        ),
      );
      await tester.pump();
      expect(find.text('Bước 1: Bật Loopback trên soundcard'), findsOneWidget);
      expect(find.text('Đang dùng: USB / soundcard'), findsOneWidget);
      await tester.tap(find.text('Tiếp'));
      await tester.pump();
      expect(find.text('Bước 2: Đo qua soundcard'), findsOneWidget);
      await tester.tap(find.text('Bắt đầu đo'));
      await tester.pump();
      expect(find.textContaining('nút Loopback'), findsOneWidget);
      await tester.tap(find.text('Đo lại'));
      await tester.pump();
      next = _latency(
        70,
      ); // soundcard: không đánh giá "trễ nhiều", chỉ để căn bản thu
      await tester.tap(find.text('Bắt đầu đo'));
      await tester.pump();
      expect(find.text('Đã đo qua soundcard'), findsOneWidget);
      expect(find.textContaining('tắt Loopback'), findsOneWidget);
      expect(find.textContaining('70 ms'), findsOneWidget);
    });
  });

  group('trang Cài đặt', () {
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

    Future<void> close(WidgetTester tester) async {
      for (var i = 0; i < 10; i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 5)),
        );
        await tester.pump(const Duration(milliseconds: 20));
      }
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(LibraryDb.closeAll);
    }

    testWidgets(
      'các nhóm, lựa chọn xuất / tự nghe lại giọng được lưu, bộ nhớ',
      (tester) async {
        tester.view.physicalSize = const Size(800, 3000);
        tester.view.devicePixelRatio = 1;
        addTearDown(tester.view.reset);
        final lib = SongLibrary(baseDir: dir);
        await tester.runAsync(() async {
          final f = File('${dir.path}/a.mp3')
            ..writeAsBytesSync(List.filled(2048, 1));
          await lib.add(f.path, move: true);
        });
        final service = AudioService.test(
          () => throw StateError('không mở engine trong test này'),
        );
        await tester.pumpWidget(
          MaterialApp(
            home: SettingsPage(engineInfo: _ok, library: lib, service: service),
          ),
        );
        await settle(tester, () => find.text('2 KB').evaluate().isNotEmpty);
        for (final h in [
          'Âm thanh',
          'Xuất',
          'Bộ nhớ',
          'Trợ giúp',
          'Giới thiệu',
          'Nâng cao',
        ]) {
          expect(find.text(h), findsOneWidget, reason: h);
        }
        expect(find.text('Micro'), findsOneWidget);
        expect(find.text('Tự động'), findsOneWidget);
        expect(find.text('Phòng thử'), findsOneWidget);
        expect(find.text('ToneCore v0.9.0 – engine OK'), findsOneWidget);

        await tester.tap(find.text('Rất cao'));
        await tester.tap(find.text('Video 720p'));
        await tester.tap(find.text('Cân bằng âm lượng cho mạng xã hội'));
        await tester.tap(find.text('Tự bật nghe lại giọng khi cắm tai nghe'));
        await tester.pump();
        expect(service.autoMonitor, isFalse);
        // Cho các lệnh ghi (chạy trong đồng hồ giả) xong rồi mới đọc lại bằng CSDL thật — đọc khi còn
        // lệnh ghi dở thì hai bên chờ nhau mãi.
        for (var i = 0; i < 10; i++) {
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 5)),
          );
          await tester.pump(const Duration(milliseconds: 20));
        }
        final s = AppSettings(baseDir: dir);
        final saved = await tester.runAsync(s.exportPrefs);
        final auto = await tester.runAsync(s.autoMonitor);
        expect(
          saved,
          const ExportPrefs(aacBitrate: 256000, hd720: true, normalize: false),
        );
        expect(auto, isFalse);

        // Xoá tất cả bài hát (có hỏi lại).
        await tester.tap(
          find.descendant(
            of: find.widgetWithText(ListTile, 'Bài hát'),
            matching: find.text('Xoá'),
          ),
        );
        await tester.pumpAndSettle();
        expect(find.text('Xoá tất cả bài hát?'), findsOneWidget);
        await tester.tap(find.widgetWithText(FilledButton, 'Xoá'));
        await settle(tester, () => find.text('2 KB').evaluate().isEmpty);
        await close(tester);
      },
      skip: false,
    );

    testWidgets('chế độ soundcard: bật → chọn hiệu ứng, lưu lại (B6)', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(800, 3000);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final service = AudioService.test(() => throw StateError('không dùng'));
      await tester.pumpWidget(
        MaterialApp(
          home: SettingsPage(
            engineInfo: _ok,
            library: SongLibrary(baseDir: dir),
            service: service,
          ),
        ),
      );
      await settle(
        tester,
        () => find.text('Cắm soundcard vào là app hỏi').evaluate().isNotEmpty,
      );
      expect(find.text('Vang của soundcard'), findsNothing);
      await tester.tap(find.text('Chế độ soundcard'));
      await tester.pump();
      expect(service.soundcardMode, isTrue);
      expect(find.text('Vang của soundcard'), findsOneWidget);
      expect(find.text(soundcardFxHelp.soundcard), findsOneWidget);
      expect(find.text('Đo độ trễ qua soundcard'), findsOneWidget);
      await tester.tap(find.text('Hiệu ứng của app'));
      await tester.pump();
      expect(service.soundcardFx, SoundcardFx.app);
      expect(find.text(soundcardFxHelp.app), findsOneWidget);
      for (var i = 0; i < 10; i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 5)),
        );
        await tester.pump(const Duration(milliseconds: 20));
      }
      final s = AppSettings(baseDir: dir);
      expect(await tester.runAsync(s.soundcardMode), isTrue);
      expect(await tester.runAsync(s.soundcardFx), SoundcardFx.app);
      await close(tester);
    });

    testWidgets('máy nhỏ 320 px, chữ 130%: không tràn', (tester) async {
      tester.view.physicalSize = const Size(640, 1136);
      tester.view.devicePixelRatio = 2;
      tester.platformDispatcher.textScaleFactorTestValue = 1.3;
      addTearDown(tester.view.reset);
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      // Bật chế độ soundcard: hiện cả phần chọn hiệu ứng.
      await tester.runAsync(
        () => AppSettings(baseDir: dir).setSoundcardMode(true),
      );
      await tester.pumpWidget(
        MaterialApp(
          home: SettingsPage(
            engineInfo: _ok,
            library: SongLibrary(baseDir: dir),
            service: AudioService.test(() => throw StateError('không dùng')),
          ),
        ),
      );
      await settle(tester, () => find.text('Đo độ trễ').evaluate().isNotEmpty);
      await tester.pump(const Duration(milliseconds: 100));
      expect(tester.takeException(), isNull);
      for (var i = 0; i < 6; i++) {
        await tester.drag(find.byType(ListView), const Offset(0, -500));
        await tester.pump();
        expect(tester.takeException(), isNull);
      }
      await close(tester);
    });
  });
}
