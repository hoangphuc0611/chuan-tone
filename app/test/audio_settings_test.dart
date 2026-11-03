// Cài đặt âm thanh (M8 B4) với engine thật, backend NULL:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/audio_settings_test.dart

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/pages/mic_page.dart';
import 'package:chuantone/widgets/input_level_meter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('audio_settings'));
  tearDown(() => dir.deleteSync(recursive: true));
  tearDownAll(LibraryDb.closeAll);

  ToneCoreEngine make() => ToneCoreEngine.create(maxBlockFrames: 256);

  /// Chờ sự kiện engine tới AudioService (engine hỏi sự kiện mỗi ~16 ms).
  Future<void> events() =>
      Future<void>.delayed(const Duration(milliseconds: 80));

  test(
    'tự bật nghe lại giọng khi cắm tai nghe, tắt khi về loa ngoài',
    () async {
      final s = AudioService.test(make, settings: AppSettings(baseDir: dir));
      addTearDown(s.shutdown);
      expect(await s.ensureStarted(), isTrue);
      final e = s.engine!;
      expect(s.autoMonitor, isTrue);
      expect(e.monitorEnabled, isFalse);
      e.setRouteHint(AudioRoute.wired);
      await events();
      expect(e.monitorEnabled, isTrue);
      e.setRouteHint(AudioRoute.speaker);
      await events();
      expect(e.monitorEnabled, isFalse);
      e.setRouteHint(AudioRoute.usb);
      await events();
      expect(e.monitorEnabled, isTrue);
      // Tắt tự động: đổi đường tiếng không động tới nghe lại giọng.
      s.updateAutoMonitor(false);
      e.setRouteHint(AudioRoute.speaker);
      await events();
      expect(e.monitorEnabled, isTrue);
    },
    skip: skip,
  );

  test(
    'cài đặt đọc lúc mở: micro đã chọn tới engine; tắt tự nghe lại giọng',
    () async {
      final settings = AppSettings(baseDir: dir);
      await settings.setInputPreference(InputKind.headset);
      await settings.setAutoMonitor(false);
      final s = AudioService.test(make, settings: settings);
      addTearDown(s.shutdown);
      await s.ensureStarted();
      final e = s.engine!;
      expect(s.inputPreference, InputKind.headset);
      expect(e.metrics.inputPreference, InputKind.headset);
      expect(e.inputWanted, InputKind.headset);
      e.setRouteHint(AudioRoute.wired);
      await events();
      expect(e.monitorEnabled, isFalse);

      await s.setInputPreference(InputKind.usb);
      expect(e.metrics.inputPreference, InputKind.usb);
      expect(await settings.inputPreference(), InputKind.usb);
    },
    skip: skip,
  );

  testWidgets(
    'trang Micro: chọn micro, thanh mức theo giọng, cảnh báo vỡ tiếng',
    (tester) async {
      final s = AudioService.test(make);
      await tester.pumpWidget(MaterialApp(home: MicPage(service: s)));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 150));
      expect(find.text('Tự động'), findsOneWidget);
      expect(find.text('Soundcard / micro USB'), findsOneWidget);
      expect(find.text('Đang dùng: chưa rõ'), findsOneWidget);
      InputLevelMeter meter() =>
          tester.widget<InputLevelMeter>(find.byType(InputLevelMeter));
      expect(meter().peakDb, -120);

      // Giọng giả ở micro: biên độ 0,5 → −6 dBFS.
      final e = s.engine!;
      final block = Float32List(256);
      for (var b = 0; b < 30; b++) {
        for (var i = 0; i < 256; i++) {
          block[i] = 0.5 * math.sin(2 * math.pi * 300 * (b * 256 + i) / 48000);
        }
        e.processOffline(block, 256);
      }
      await tester.pump(const Duration(milliseconds: 150));
      expect(meter().peakDb, closeTo(-6.0, 0.1));
      expect(find.byKey(const ValueKey('mic-clipping')), findsNothing);

      // Chạm 0 dBFS → cảnh báo.
      e.processOffline(Float32List(256)..fillRange(0, 40, 1.0), 256);
      await tester.pump(const Duration(milliseconds: 150));
      expect(find.byKey(const ValueKey('mic-clipping')), findsOneWidget);

      await tester.tap(find.text('Soundcard / micro USB'));
      await tester.pump();
      expect(s.inputPreference, InputKind.usb);
      expect(e.metrics.inputPreference, InputKind.usb);
      expect(e.inputWanted, InputKind.usb);

      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(s.shutdown);
    },
    skip: skip != null,
  );
}
