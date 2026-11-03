// Màn hát (M8 B2) với engine thật, backend NULL:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/sing_page_test.dart

import 'dart:async';
import 'dart:io';

import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/data/song_ref.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/sing_controller.dart';
import 'package:chuantone/pages/sing_page.dart';
import 'package:chuantone/widgets/lyrics_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  late String wav;

  setUpAll(() {
    if (skip != null) return;
    wav =
        '${Directory.systemTemp.createTempSync('sing_page_src').path}/Duyên Phận Bolero.wav';
    writeCadenceWav(wav);
  });

  setUp(() {
    if (skip != null) return;
    dir = Directory.systemTemp.createTempSync('sing_page');
  });

  tearDown(() async {
    if (skip != null) return;
    dir.deleteSync(recursive: true);
  });

  /// Chạy tới khi [done] (IO thật + engine thật chạy ngoài đồng hồ giả của test).
  Future<void> settle(WidgetTester tester, bool Function() done) async {
    for (var i = 0; i < 400 && !done(); i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 10)),
      );
      await tester.pump(const Duration(milliseconds: 20));
    }
    expect(done(), isTrue, reason: 'hết giờ chờ');
  }

  /// Mở Màn hát cho bài thử; trả bộ điều khiển (test huỷ ở [close]).
  Future<(SingController, AudioService)> open(
    WidgetTester tester, {
    bool tips = false,
    String? lyrics,
  }) async {
    final song = await tester.runAsync(() async {
      final lib = SongLibrary(baseDir: dir);
      final s = await lib.add(wav);
      if (lyrics != null) {
        final f = File('${dir.path}/loi.lrc')..writeAsStringSync(lyrics);
        await lib.setLyrics(s.name, s.size, f.path);
      }
      if (!tips) {
        // Đã xem mẹo: không che màn khi test các nút.
        await LibraryDb.setSetting(dir, 'tipShown.sing', true);
      }
      return s.ref;
    });
    final service = AudioService.test(
      () => ToneCoreEngine.create(maxBlockFrames: 256),
    );
    final c = SingController(
      service: service,
      baseDir: dir,
      autoTick: false,
      countdownStep: Duration.zero,
    );
    await tester.pumpWidget(
      MaterialApp(
        home: SingPage(song: song!, controller: c),
      ),
    );
    await settle(tester, () => c.analysis != null && !c.analyzing);
    await tester.pump();
    return (c, service);
  }

  /// Gỡ màn, huỷ bộ điều khiển, tắt engine, đóng CSDL. Việc có future của vùng thật (huỷ đăng ký
  /// sự kiện, đóng CSDL) phải chạy trong runAsync, không thì đồng hồ giả của test đứng.
  Future<void> close(
    WidgetTester tester,
    SingController c,
    AudioService s,
  ) async {
    await tester.pumpWidget(const SizedBox());
    c.dispose();
    await tester.runAsync(s.shutdown);
    await tester.runAsync(LibraryDb.closeAll);
  }

  testWidgets('mở bài: tên bài, tone thật to theo bài, thể loại theo tên', (
    tester,
  ) async {
    final (c, s) = await open(tester);
    expect(find.text('Duyên Phận Bolero'), findsOneWidget);
    expect(find.text('Rê trưởng (D)'), findsOneWidget);
    expect(find.text('theo bài'), findsOneWidget);
    expect(find.text('Tone Gốc'), findsOneWidget);
    expect(find.text('Gợi ý theo tên bài'), findsOneWidget);
    final bolero = tester.widget<ChoiceChip>(
      find.widgetWithText(ChoiceChip, 'Bolero'),
    );
    expect(bolero.selected, isTrue);
    expect(find.text('Thu'), findsOneWidget);
    await close(tester, c, s);
  }, skip: skip != null);

  testWidgets('tone ±, thể loại, chỉnh giọng: áp ngay, nhớ theo bài', (
    tester,
  ) async {
    final (c, s) = await open(tester);
    await tester.tap(find.byTooltip('Nâng tone'));
    await tester.pump();
    expect(find.text('Tone +1'), findsOneWidget);
    expect(s.engine!.transpose, 1);

    await tester.ensureVisible(find.widgetWithText(ChoiceChip, 'Ballad'));
    await tester.tap(find.widgetWithText(ChoiceChip, 'Ballad'));
    await tester.pump();
    expect(s.genre, Genre.ballad);
    expect(find.text('Đã chọn cho bài này'), findsOneWidget);

    await tester.ensureVisible(find.widgetWithText(ChoiceChip, 'Robot'));
    await tester.tap(find.widgetWithText(ChoiceChip, 'Robot'));
    await tester.pump();
    expect(s.correction, CorrectionPreset.robot);
    expect(find.textContaining('Bật "Nghe lại giọng"'), findsOneWidget);

    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 50)),
    );
    final prefs = SongPrefs(baseDir: dir);
    final song = c.song!;
    final saved = await tester.runAsync(
      () => prefs.transposeFor(name: song.name, size: song.size),
    );
    expect(saved, 1);
    await close(tester, c, s);
  }, skip: skip != null);

  testWidgets('sửa tone của bài qua menu ⋮; nút loa mở âm lượng', (
    tester,
  ) async {
    final (c, s) = await open(tester);
    await tester.tap(find.byTooltip('Thêm'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Sửa tone của bài'));
    await tester.pumpAndSettle();
    expect(find.text('Sửa tone của bài'), findsOneWidget); // tiêu đề hộp
    await tester.tap(find.text('Thứ'));
    await tester.pump();
    await tester.tap(find.text('Lưu'));
    await settle(tester, () => c.manualKey != null);
    await tester.pumpAndSettle();
    expect(c.manualKey, const MusicKey(2, MusicScale.naturalMinor));
    expect(find.text('Rê thứ (Dm)'), findsOneWidget);
    expect(find.text('tone bạn đã sửa'), findsOneWidget);

    await tester.tap(find.byTooltip('Âm lượng'));
    await tester.pumpAndSettle();
    expect(find.text('Nghe lại giọng'), findsOneWidget);
    await tester.tap(find.text('Nghe lại giọng'));
    await tester.pump();
    expect(c.monitorOn, isTrue);
    expect(s.engine!.monitorEnabled, isTrue);
    await close(tester, c, s);
  }, skip: skip != null);

  testWidgets('mẹo lần đầu: Tone → Chỉnh giọng → Thu, chỉ hiện một lần', (
    tester,
  ) async {
    final (c, s) = await open(tester, tips: true);
    await settle(tester, () => find.text('Tiếp (1/3)').evaluate().isNotEmpty);
    await tester.pumpAndSettle();
    expect(find.text('Tone'), findsOneWidget);
    await tester.tap(find.text('Tiếp (1/3)'));
    await tester.pumpAndSettle();
    expect(
      find.text('Chỉnh giọng'),
      findsNWidgets(2),
    ); // tiêu đề mục + bong bóng
    await tester.tap(find.text('Tiếp (2/3)'));
    await tester.pumpAndSettle();
    expect(find.text('Đã hiểu'), findsOneWidget);
    await tester.tap(find.text('Đã hiểu'));
    await tester.pumpAndSettle();
    expect(find.text('Đã hiểu'), findsNothing);
    await close(tester, c, s);

    // Mở lại: không hiện nữa.
    final (c2, s2) = await open(tester, tips: true);
    for (var i = 0; i < 10; i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 10)),
      );
      await tester.pump(const Duration(milliseconds: 50));
    }
    expect(find.textContaining('Tiếp ('), findsNothing);
    await close(tester, c2, s2);
  }, skip: skip != null);

  testWidgets('lời chạy chữ + menu Lời bài hát (lời sớm / muộn, bỏ lời)', (
    tester,
  ) async {
    final (c, s) = await open(
      tester,
      lyrics: '[00:00.00]Câu đầu tiên\n[00:08.00]Câu thứ hai\n',
    );
    expect(find.byType(LyricsView), findsOneWidget);
    c.seek(const Duration(seconds: 1));
    await tester.pump();
    expect(find.textContaining('Câu đầu tiên'), findsOneWidget);
    expect(find.text('Câu thứ hai'), findsOneWidget);

    await tester.tap(find.byTooltip('Thêm'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Lời bài hát'));
    await tester.pumpAndSettle();
    expect(find.text('Đã có lời: 2 dòng.'), findsOneWidget);
    expect(find.text('Lời đúng như file'), findsOneWidget);
    await tester.tap(find.text('Lời sớm hơn'));
    await tester.pump();
    expect(find.text('Lời sớm hơn 0,5 s'), findsOneWidget);
    expect(c.lyricsOffsetMs, 500);
    await tester.tap(find.text('Bỏ lời'));
    await settle(tester, () => c.lyrics == null);
    await tester.pumpAndSettle();
    expect(find.text('Chọn file lời'), findsOneWidget);
    await tester.tapAt(const Offset(10, 10)); // đóng bảng
    await tester.pumpAndSettle();
    expect(find.byType(LyricsView), findsNothing);
    await close(tester, c, s);
  }, skip: skip != null);

  for (final width in [320.0, 375.0]) {
    testWidgets('máy nhỏ ${width.round()} px, chữ 130%: không tràn, Thu ở đáy', (
      tester,
    ) async {
      tester.view.physicalSize = Size(width * 2, 568 * 2);
      tester.view.devicePixelRatio = 2;
      tester.platformDispatcher.textScaleFactorTestValue = 1.3;
      addTearDown(tester.view.reset);
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      final (c, s) = await open(tester);
      expect(tester.takeException(), isNull);
      final thu = find.text('Thu');
      expect(thu, findsOneWidget);
      expect(tester.getBottomLeft(thu).dy, greaterThan(568 - 120));
      expect(find.text('Rê trưởng (D)').hitTestable(), findsOneWidget);
      // Chế độ soundcard: thẻ "vang của soundcard" thay chip thể loại, vẫn vừa.
      s.soundcardMode = true;
      s.engine!.setRouteHint(AudioRoute.usb);
      await settle(tester, () => s.soundcardActive);
      c.refresh();
      await tester.pump();
      expect(find.byKey(const ValueKey('sing-soundcard-fx')), findsOneWidget);
      expect(tester.takeException(), isNull);
      // Cuộn hết phần trên: vẫn không tràn, thanh phát vẫn ở đáy.
      await tester.drag(
        find.byType(SingleChildScrollView).first,
        const Offset(0, -600),
      );
      await tester.pump();
      expect(tester.takeException(), isNull);
      expect(find.text('Thu').hitTestable(), findsOneWidget);
      // Đang thu: nút thành "Dừng · 0:00", vẫn vừa.
      await tester.tap(thu);
      await settle(tester, () => c.recording);
      await tester.pump();
      expect(find.textContaining('Dừng ·'), findsOneWidget);
      expect(find.byKey(const ValueKey('sing-loopback-tip')), findsOneWidget);
      expect(tester.takeException(), isNull);
      await close(tester, c, s);
    }, skip: skip != null);
  }

  testWidgets(
    'cắm soundcard: hỏi một lần → tắt nghe lại giọng, vang của soundcard; Thu nhắc tắt Loopback',
    (tester) async {
      tester.view.physicalSize = const Size(800, 2400);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final (c, s) = await open(tester);
      final e = s.engine!;
      expect(find.text('Bạn vừa cắm soundcard?'), findsNothing);
      e.setRouteHint(AudioRoute.usb);
      await settle(tester, () => s.route == AudioRoute.usb);
      c.refresh();
      await tester.pumpAndSettle();
      expect(find.text('Bạn vừa cắm soundcard?'), findsOneWidget);
      expect(e.monitorEnabled, isTrue); // chưa chọn: như tai nghe
      await tester.tap(find.text('Dùng chế độ soundcard'));
      await tester.pumpAndSettle();
      expect(s.soundcardActive, isTrue);
      expect(c.monitorOn, isFalse);
      expect(find.byKey(const ValueKey('sing-soundcard-fx')), findsOneWidget);
      expect(find.widgetWithText(ChoiceChip, 'Bolero'), findsNothing);
      expect(
        find.textContaining('giọng đã chỉnh có trong bản thu'),
        findsOneWidget,
      );
      bool? saved;
      unawaited(c.settings.soundcardMode().then((v) => saved = v));
      await settle(tester, () => saved != null);
      expect(saved, isTrue);

      // Dùng hiệu ứng của app: chip thể loại trở lại, kèm lời giải thích.
      await tester.tap(find.text('Dùng hiệu ứng của app'));
      await tester.pump();
      expect(find.widgetWithText(ChoiceChip, 'Bolero'), findsOneWidget);
      expect(find.text('Dùng vang của soundcard'), findsOneWidget);

      // Rút ra cắm lại: không hỏi nữa, vẫn tắt nghe lại giọng.
      e.setRouteHint(AudioRoute.speaker);
      await settle(tester, () => s.route == AudioRoute.speaker);
      e.setRouteHint(AudioRoute.usb);
      await settle(tester, () => s.route == AudioRoute.usb);
      c.refresh();
      await tester.pumpAndSettle();
      expect(find.text('Bạn vừa cắm soundcard?'), findsNothing);
      expect(c.monitorOn, isFalse);

      // Thu: nhắc tắt Loopback mấy giây đầu.
      expect(find.byKey(const ValueKey('sing-loopback-tip')), findsNothing);
      await tester.tap(find.text('Thu'));
      await settle(tester, () => c.recording);
      c.refresh();
      await tester.pump();
      expect(find.byKey(const ValueKey('sing-loopback-tip')), findsOneWidget);
      await close(tester, c, s);
    },
    skip: skip != null,
  );

  testWidgets('bài không mở được: báo lỗi bằng thanh thông báo', (
    tester,
  ) async {
    final service = AudioService.test(
      () => ToneCoreEngine.create(maxBlockFrames: 256),
    );
    final c = SingController(service: service, baseDir: dir, autoTick: false);
    await tester.pumpWidget(
      MaterialApp(
        home: SingPage(
          song: SongRef(
            path: '${dir.path}/khong-co.mp3',
            name: 'khong-co.mp3',
            size: 1,
          ),
          controller: c,
        ),
      ),
    );
    await settle(tester, () => c.message != null);
    await tester.pump();
    expect(find.byType(SnackBar), findsOneWidget);
    expect(find.textContaining('Không mở được bài này'), findsOneWidget);
    expect(find.text('Chi tiết'), findsOneWidget); // chữ kỹ thuật để ở nút này
    await close(tester, c, service);
  }, skip: skip != null);
}
