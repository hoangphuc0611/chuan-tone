import 'dart:io';

import 'package:chuantone/app.dart';
import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine_info.dart';
import 'package:chuantone/pages/onboarding_page.dart';
import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

LatencyResult _latency(double ms) => LatencyResult(
  valid: true,
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

/// Dịch vụ âm thanh không mở được thiết bị (test không có engine).
AudioService _noEngine() =>
    AudioService.test(() => throw StateError('không có engine trong test'));

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('onboarding'));
  tearDown(() => dir.deleteSync(recursive: true));

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

  /// Cho lệnh ghi CSDL còn dở chạy xong, gỡ màn, đóng CSDL.
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
    'đi hết 6 bước: micro bị từ chối → để sau, đo, tai nghe, thử giọng, hát bài mẫu',
    (tester) async {
      tester.view.physicalSize = const Size(800, 1600);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final service = _noEngine();
      OnboardingExit? exit;
      var asked = 0;
      await tester.pumpWidget(
        MaterialApp(
          home: OnboardingPage(
            service: service,
            settings: AppSettings(baseDir: dir),
            micStatus: () async => MicStatus.undetermined,
            requestMic: () async {
              asked++;
              return false;
            },
            measure: () async => _latency(24),
            onFinish: (e) => exit = e,
          ),
        ),
      );
      expect(find.text('Bấm bài là hát'), findsOneWidget);
      expect(find.text('Tự chỉnh giọng cho đúng nốt'), findsOneWidget);
      await tester.tap(find.text('Bắt đầu'));
      await settle(
        tester,
        () => find.text('Cho phép dùng micro').evaluate().isNotEmpty,
      );
      await tester.tap(find.text('Cho phép dùng micro'));
      await settle(
        tester,
        () => find
            .byKey(const ValueKey('onboarding-mic-denied'))
            .evaluate()
            .isNotEmpty,
      );
      expect(asked, 1);
      expect(find.text('Mở Cài đặt'), findsOneWidget);
      await tester.tap(find.text('Để sau'));
      await settle(
        tester,
        () => find.text('Đo nhanh (5 giây)').evaluate().isNotEmpty,
      );
      await tester.tap(find.byKey(const ValueKey('onboarding-measure')));
      await settle(
        tester,
        () => find.text('Máy bạn hát mượt').evaluate().isNotEmpty,
      );
      await tester.tap(find.text('Tiếp'));
      await settle(tester, () => find.text('Tai nghe').evaluate().isNotEmpty);
      expect(
        find.text(
          'Đang dùng loa ngoài: vẫn hát được nhưng không nghe lại giọng.',
        ),
        findsOneWidget,
      );
      expect(service.genre, Genre.dry);
      await tester.tap(find.text('Tiếp'));
      await settle(tester, () => find.text('Thử giọng').evaluate().isNotEmpty);
      expect(service.genre, Genre.ballad); // có vang để nghe thử
      await tester.tap(find.text('Chưa'));
      await tester.pump();
      expect(find.textContaining('Tăng âm lượng máy'), findsOneWidget);
      await tester.tap(find.text('Tiếp'));
      await settle(
        tester,
        () => find.text('Xong! Hát thôi').evaluate().isNotEmpty,
      );
      expect(service.genre, Genre.dry); // trả lại thể loại cũ
      await tester.tap(find.text('Hát thử bài mẫu'));
      await settle(tester, () => exit != null);
      expect(exit, OnboardingExit.sample);
      await close(tester);
    },
  );

  testWidgets('đã có quyền micro → "Tiếp"; "Bỏ qua" lưu đã xong', (
    tester,
  ) async {
    OnboardingExit? exit;
    await tester.pumpWidget(
      MaterialApp(
        home: OnboardingPage(
          service: _noEngine(),
          settings: AppSettings(baseDir: dir),
          micStatus: () async => MicStatus.granted,
          requestMic: () async => fail('không được hỏi lại'),
          measure: () async => null,
          onFinish: (e) => exit = e,
        ),
      ),
    );
    await tester.tap(find.text('Bắt đầu'));
    await settle(tester, () => find.text('Tiếp').evaluate().isNotEmpty);
    expect(find.text('Cho phép dùng micro'), findsNothing);
    await tester.tap(find.text('Bỏ qua'));
    await settle(tester, () => exit != null);
    expect(exit, OnboardingExit.home);
    await close(tester);
  });

  testWidgets(
    'mở app lần đầu vào hướng dẫn; bỏ qua → Trang chủ; lần sau thẳng Trang chủ',
    (tester) async {
      const ok = EngineInfo(ok: true, version: '0.9.0', apiVersion: '0.9.0');
      await tester.pumpWidget(
        ChuanToneApp(
          engineInfo: ok,
          library: SongLibrary(baseDir: dir),
        ),
      );
      await settle(tester, () => find.text('Bắt đầu').evaluate().isNotEmpty);
      await tester.tap(find.text('Bỏ qua'));
      await settle(
        tester,
        () => find.text('Thêm bài hát').evaluate().isNotEmpty,
      );
      await close(tester);

      await tester.pumpWidget(
        ChuanToneApp(
          engineInfo: ok,
          library: SongLibrary(baseDir: dir),
        ),
      );
      await settle(
        tester,
        () => find.text('Thêm bài hát').evaluate().isNotEmpty,
      );
      expect(find.text('Bắt đầu'), findsNothing);
      await close(tester);
    },
  );

  testWidgets('máy nhỏ 320 px, chữ 130%: từng bước không tràn', (tester) async {
    tester.view.physicalSize = const Size(640, 1136);
    tester.view.devicePixelRatio = 2;
    tester.platformDispatcher.textScaleFactorTestValue = 1.3;
    addTearDown(tester.view.reset);
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    await tester.pumpWidget(
      MaterialApp(
        home: OnboardingPage(
          service: _noEngine(),
          settings: AppSettings(baseDir: dir),
          micStatus: () async => MicStatus.denied,
          requestMic: () async => false,
          measure: () async => _latency(70),
          onFinish: (_) {},
        ),
      ),
    );
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Bắt đầu'));
    await settle(tester, () => find.text('Mở Cài đặt').evaluate().isNotEmpty);
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Để sau'));
    await settle(
      tester,
      () => find.text('Đo nhanh (5 giây)').evaluate().isNotEmpty,
    );
    await tester.tap(find.byKey(const ValueKey('onboarding-measure')));
    await settle(tester, () => find.text('Trễ nhiều').evaluate().isNotEmpty);
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Tiếp'));
    await settle(tester, () => find.text('Tai nghe').evaluate().isNotEmpty);
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Tiếp'));
    await settle(tester, () => find.text('Thử giọng').evaluate().isNotEmpty);
    await tester.tap(find.text('Chưa'));
    await tester.pump();
    expect(tester.takeException(), isNull);
    await close(tester);
  });
}
