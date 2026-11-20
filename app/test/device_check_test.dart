// "Kiểm tra máy" (M9 B4): engine phụ chạy giọng + beat giả qua cả chuỗi ở isolate riêng.

import 'dart:async';
import 'dart:io';

import 'package:chuantone/engine/device_check.dart';
import 'package:chuantone/pages/device_check_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'helpers/engine_audio.dart';

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('devcheck'));
  tearDown(() => dir.deleteSync(recursive: true));

  test('giọng giả có cao độ, beat giả stereo', () {
    final v = synthCheckVoice(48000);
    expect(v.length, greaterThan(14 * 48000));
    expect(v.reduce((a, b) => a.abs() > b.abs() ? a : b).abs(), lessThan(1));
    final b = synthCheckBeat(48000);
    expect(b.length, 2 * 12 * 48000);
  });

  test(
    'chạy 3 s âm thanh: số đo hợp lý, không mẫu lỗi, xoá beat tạm',
    () async {
      final progress = <double>[];
      final run = startDeviceCheck(
        audio: const Duration(seconds: 3),
        tempDir: dir.path,
        onProgress: progress.add,
      );
      final r = await run.result;
      expect(r, isNotNull);
      expect(r!.blocks, 3 * 48000 ~/ 256);
      expect(r.badSamples, 0);
      expect(r.peak, greaterThan(0.05)); // có tiếng (giọng nghe lại + beat)
      expect(r.meanUs, greaterThan(0));
      expect(r.p99Us, greaterThanOrEqualTo(r.meanUs * 0.5));
      expect(r.maxUs, greaterThanOrEqualTo(r.p99Us));
      expect(r.realtimeFactor, greaterThan(1));
      expect(r.level, isNot(DeviceCheckLevel.broken));
      expect(progress, isNotEmpty);
      expect(dir.listSync(), isEmpty);
    },
    skip: skip,
  );

  test('huỷ giữa chừng: kết quả null', () async {
    late DeviceCheckRun run;
    var cancelled = false;
    run = startDeviceCheck(
      audio: const Duration(minutes: 30),
      tempDir: dir.path,
      onProgress: (p) {
        if (!cancelled) {
          cancelled = true;
          run.cancel();
        }
      },
    );
    expect(await run.result.timeout(const Duration(seconds: 60)), isNull);
  }, skip: skip);

  test('mức đánh giá theo CPU trung bình', () {
    DeviceCheckResult r(double mean, {int bad = 0}) => DeviceCheckResult(
      audio: const Duration(seconds: 30),
      blocks: 100,
      blockFrames: 256,
      sampleRate: 48000,
      meanUs: mean,
      p99Us: mean * 4, // p99 cao (hệ điều hành chen ngang) không làm đổi mức
      maxUs: mean * 10,
      elapsed: const Duration(seconds: 1),
      badSamples: bad,
      peak: 0.5,
    );
    // Một khối 256 mẫu = 5333 µs.
    expect(r(1000).level, DeviceCheckLevel.good); // 19%
    expect(r(2000).level, DeviceCheckLevel.weak); // 37%
    expect(r(3000).level, DeviceCheckLevel.poor); // 56%
    expect(r(100, bad: 3).level, DeviceCheckLevel.broken);
    expect(r(1000).meanLoad, closeTo(0.1875, 1e-4));
  });
  testWidgets('màn Kiểm tra máy: tiến độ, kết quả, huỷ', (tester) async {
    late Completer<DeviceCheckResult?> done;
    void Function(double)? report;
    var cancelled = false;
    DeviceCheckRun fake({
      required Duration audio,
      required String tempDir,
      void Function(double progress)? onProgress,
    }) {
      done = Completer();
      report = onProgress;
      return DeviceCheckRun.test(done.future, () => cancelled = true);
    }

    await tester.pumpWidget(const MaterialApp(home: SizedBox()));
    tester.view.physicalSize = const Size(1200, 3000); // thấy cả màn, không phải cuộn
    tester.view.devicePixelRatio = 1.5;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MaterialApp(home: DeviceCheckPage(start: fake)));
    await tester.tap(find.text('Kiểm tra nhanh'));
    await tester.pump();
    report!(0.42);
    await tester.pump();
    expect(find.text('Đang kiểm tra… 42%'), findsOneWidget);
    done.complete(
      DeviceCheckResult(
        audio: const Duration(seconds: 30),
        blocks: 5625,
        blockFrames: 256,
        sampleRate: 48000,
        meanUs: 100,
        p99Us: 160,
        maxUs: 400,
        elapsed: const Duration(seconds: 2),
        badSamples: 0,
        peak: 0.98,
      ),
    );
    await tester.pumpAndSettle();
    expect(find.text('Máy đủ khoẻ — chỉnh giọng chạy mượt.'), findsOneWidget);
    expect(find.text('1,9% một nhân (lúc nặng nhất 3,0%)'), findsOneWidget);
    expect(find.text('Gửi kết quả'), findsOneWidget);

    // Chạy lại rồi huỷ.
    await tester.tap(find.text('Kiểm tra lâu'));
    await tester.pump();
    await tester.tap(find.text('Huỷ'));
    await tester.pump();
    expect(cancelled, isTrue);
    expect(find.text('Kiểm tra nhanh'), findsOneWidget);
  });

  test('báo cáo gửi nhóm phát triển', () {
    final text = deviceCheckReport(
      const DeviceCheckResult(
        audio: Duration(minutes: 30),
        blocks: 337500,
        blockFrames: 256,
        sampleRate: 48000,
        meanUs: 100,
        p99Us: 160,
        maxUs: 400,
        elapsed: Duration(seconds: 70),
        badSamples: 0,
        peak: 0.981,
      ),
      at: DateTime(2026, 11, 20, 10, 5),
      device: 'Apple · iPhone14,2 · iOS 26.6.2',
      appVersion: '1.0.0',
      engineVersion: '0.9.1',
      latency: const ['Loa ngoài 12,6 ms'],
      xruns: 0,
      health: '3 phiên · 0 lần thoát bất thường · 0 lỗi',
    );
    expect(text, contains('Thời điểm: 20/11/2026 10:05'));
    expect(text, contains('Kiểm tra: 30 phút âm thanh'));
    expect(text, contains('CPU trung bình 1,9% một nhân · p99 3,0%'));
    expect(text, contains('nhanh hơn thời gian thực 53 lần'));
    expect(text, contains('Độ trễ đã đo: Loa ngoài 12,6 ms'));
    expect(text, contains('Xrun phiên hiện tại: 0'));
    expect(text, contains('Sức khoẻ app: 3 phiên'));
  });
}
