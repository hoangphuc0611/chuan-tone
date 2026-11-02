import 'dart:typed_data';

import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/sync_check.dart';
import 'package:chuantone/engine/take_alignment.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  group('chọn độ trễ để bù bản thu', () {
    final history = parseLatencyHistory([
      '2026-10-28T10:00:00,"Apple","iPhone14,2","iOS 26",speaker,remoteio,48000,0,128,12.60,8.40,0.99,3,0.050',
      'hỏng',
      '2026-10-27T10:00:00,"Apple","iPhone14,2","iOS 26",bluetooth,remoteio,48000,0,128,212.6,177.9,0.9,3,1.0',
      '2026-10-20T10:00:00,"Apple","iPhone14,2","iOS 26",speaker,remoteio,48000,0,128,20.00,8.00,0.99,3,0.050',
    ]);

    test('đọc CSV, tên máy có dấu phẩy trong ngoặc kép', () {
      expect(history, hasLength(3));
      expect(history.first.route, AudioRoute.speaker);
      expect(history.first.measuredMs, 12.6);
      expect(history.first.reportedMs, 8.4);
    });

    test('cùng đường ra → số đo mới nhất', () {
      final r = roundTripFor(
        route: AudioRoute.speaker,
        reportedMs: 9,
        history: history,
      );
      expect(r.source, LatencySource.measured);
      expect(r.ms, 12.6);
    });

    test('tai nghe có dây chưa đo → HĐH báo + chênh ở loa ngoài', () {
      final r = roundTripFor(
        route: AudioRoute.wired,
        reportedMs: 6,
        history: history,
      );
      expect(r.source, LatencySource.corrected);
      expect(r.ms, closeTo(6 + 12.6 - 8.4, 1e-9));
    });

    test('Bluetooth / chưa đo lần nào → HĐH báo', () {
      expect(
        roundTripFor(
          route: AudioRoute.bluetooth,
          reportedMs: 180,
          history: history,
        ).source,
        LatencySource.reported,
      );
      final none = roundTripFor(
        route: AudioRoute.wired,
        reportedMs: 6,
        history: const [],
      );
      expect((none.ms, none.source), (6.0, LatencySource.reported));
    });

    test('số frame dịch = độ trễ + limiter', () {
      expect(
        voiceOffsetFrames(
          roundTripMs: 12.6,
          sampleRate: 48000,
          lookaheadFrames: 48,
        ),
        605 + 48,
      );
    });
  });

  group('kiểm tra khớp tiếng', () {
    test('đo đúng độ chậm của micro giả (vang + ồn)', () {
      const rate = 48000;
      final beat = clickTrack(length: const Duration(seconds: 6));
      final onsets = detectOnsets(beat, sampleRate: rate);
      expect(onsets, hasLength(12)); // 0,25 s, 0,75 s … 5,75 s
      expect(
        onsets.first,
        inInclusiveRange(12000, 12040),
      ); // sườn lên cửa sổ Hann
      // Micro: chậm 650 frame, nhỏ hơn, có vang và ồn.
      const lag = 650;
      final dry = Float32List(beat.length);
      var seed = 1;
      for (var i = 0; i < dry.length; i++) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        final noise = (seed / 0x7fffffff - 0.5) * 0.004;
        var v = 0.0;
        if (i >= lag) v += 0.2 * beat[i - lag];
        if (i >= lag + 480) v += 0.05 * beat[i - lag - 480];
        dry[i] = v + noise;
      }
      final m = measureSync(beat: beat, dry: dry, sampleRate: rate)!;
      expect(m.pairs, 12);
      expect(m.lagFrames, inInclusiveRange(lag, lag + 10));
      expect(m.lagMs, closeTo(13.5, 0.3));
      expect(m.spreadFrames, lessThan(5));
    });

    test('micro im → null', () {
      final beat = clickTrack(length: const Duration(seconds: 3));
      expect(
        measureSync(
          beat: beat,
          dry: Float32List(beat.length),
          sampleRate: 48000,
        ),
        isNull,
      );
    });

    test('WAV 16-bit ghi rồi đọc lại', () {
      final x = Float32List.fromList([0, 0.5, -0.5, 1.2, -1, 0.25]);
      final w = parseWav(encodeWav16(x, sampleRate: 44100, channels: 2));
      expect(w.sampleRate, 44100);
      expect(w.channels, 2);
      expect(w.frames, 3);
      expect(w.channel(1)[1], closeTo(1.0, 1e-4));
      expect(w.samples[2], closeTo(-0.5, 1e-4));
    });
  });
}
