// Hình sóng bản thu (màn Nghe lại) và đồng hồ kim đo độ trễ (giao diện mới, docs/ui/redesign-plan.md).

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/take_waveform.dart';
import 'package:chuantone/widgets/latency_gauge.dart';
import 'package:chuantone/widgets/waveform_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'beat_leak_test.dart' show encodeWavFloat;

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('waveform'));
  tearDown(() => dir.deleteSync(recursive: true));

  test('cột to ở đoạn có tiếng, gần 0 ở đoạn lặng; chuẩn hoá về 1', () {
    // 2 giây: giây đầu chỉ có giọng (to), giây sau chỉ có nhạc (nhỏ hơn), ở giữa lặng 0,5 s.
    const rate = 48000;
    final dry = Float32List(2 * rate);
    final beat = Float32List(2 * rate * 2); // stereo
    for (var i = 0; i < rate ~/ 2; i++) {
      dry[i] = 0.5 * math.sin(2 * math.pi * 220 * i / rate);
    }
    for (var i = rate + rate ~/ 2; i < 2 * rate; i++) {
      final v = 0.2 * math.sin(2 * math.pi * 110 * i / rate);
      beat[2 * i] = v;
      beat[2 * i + 1] = v;
    }
    File('${dir.path}/dry.wav').writeAsBytesSync(encodeWavFloat(dry));
    File('${dir.path}/beat.wav')
        .writeAsBytesSync(encodeWav16(beat, sampleRate: rate, channels: 2));
    final w = computeWaveform(
      '${dir.path}/dry.wav',
      '${dir.path}/beat.wav',
      bins: 8,
    );
    expect(w, hasLength(8));
    expect(w.reduce(math.max), closeTo(1, 1e-6));
    expect(w[0], greaterThan(0.9)); // giọng
    expect(w[3], lessThan(0.01)); // lặng
    expect(w[7], inInclusiveRange(0.3, 0.5)); // nhạc 0,2 so với giọng 0,5
  });

  testWidgets('chạm hình sóng để tua', (tester) async {
    double? seek;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SizedBox(
            width: 400,
            child: WaveformView(
              levels: Float32List.fromList(List.filled(40, 0.5)),
              position: 0.25,
              onSeek: (f) => seek = f,
            ),
          ),
        ),
      ),
    );
    final box = tester.getRect(find.byType(WaveformView));
    await tester.tapAt(Offset(box.left + box.width * 0.75, box.center.dy));
    expect(seek, closeTo(0.75, 0.01));

    // Kéo: chỉ tua một lần khi thả tay.
    var calls = 0;
    seek = null;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SizedBox(
            width: 400,
            child: WaveformView(
              levels: Float32List.fromList(List.filled(40, 0.5)),
              position: 0.25,
              onSeek: (f) {
                calls++;
                seek = f;
              },
            ),
          ),
        ),
      ),
    );
    final g = await tester.startGesture(
      Offset(box.left + box.width * 0.2, box.center.dy),
    );
    for (var i = 1; i <= 10; i++) {
      await g.moveBy(Offset(box.width * 0.04, 0));
      await tester.pump();
    }
    expect(calls, 0);
    await g.up();
    await tester.pump();
    expect(calls, 1);
    expect(seek, closeTo(0.6, 0.03));
  });

  testWidgets('đồng hồ kim: hiện số ms', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(body: Center(child: LatencyGauge(ms: 24.4))),
      ),
    );
    expect(find.text('24 ms', findRichText: true), findsOneWidget);
  });
}
