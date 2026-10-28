// Test binding FFI trên máy dev / CI: nạp thư viện host qua biến môi trường TONECORE_LIB.
//   TONECORE_LIB=../../engine/build/host/libtonecore.dylib fvm flutter test

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  final lib = Platform.environment['TONECORE_LIB'];
  if (lib == null || lib.isEmpty) {
    test('cần TONECORE_LIB', () {
      fail(
        'Đặt TONECORE_LIB trỏ tới libtonecore build bằng CMake (xem docs/build.md)',
      );
    });
    return;
  }

  test('thông tin phiên bản', () {
    expect(ToneCore.isCompatible, isTrue);
    expect(ToneCore.version, isNotEmpty);
    expect(ToneCore.apiVersionString, '0.5.0');
  });

  test('tạo, chạy, dừng và huỷ engine', () {
    final engine = ToneCoreEngine.create();
    expect(engine.metrics.state, EngineState.stopped);
    engine.start();
    expect(engine.metrics.state, EngineState.running);
    expect(() => engine.start(), throwsA(isA<ToneCoreException>()));
    engine.stop();
    engine.dispose();
    expect(() => engine.start(), throwsStateError);
    engine.dispose(); // gọi lần hai không sao
  });

  test('cấu hình sai báo lỗi rõ ràng', () {
    expect(
      () => ToneCoreEngine.create(sampleRate: 10),
      throwsA(
        isA<ToneCoreException>().having(
          (e) => e.codeName,
          'codeName',
          'TC_ERR_INVALID_ARG',
        ),
      ),
    );
  });

  test('xử lý offline: mono → stereo, gain có làm mượt', () {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();

    // Sin 1 kHz (đường micro có lọc thông cao 80 Hz nên không dùng DC); 240 mẫu = 5 chu kỳ.
    const n = 240;
    final input = Float32List(n);
    for (var i = 0; i < n; i++) {
      input[i] = 0.25 * math.sin(2 * math.pi * 1000 * i / 48000);
    }
    double peak(Float32List out, int ch) {
      var p = 0.0;
      for (var i = 0; i < n; i++) {
        p = math.max(p, out[2 * i + ch].abs());
      }
      return p;
    }

    var out = engine.processOffline(input, n);
    expect(out.length, 2 * n);
    expect(peak(out, 0), 0.0); // nghe lại giọng mặc định tắt

    engine.monitorEnabled = true;
    expect(engine.monitorEnabled, isTrue);
    for (var i = 0; i < 40; i++) {
      out = engine.processOffline(input, n);
    }
    expect(peak(out, 0), closeTo(0.25, 0.005));
    expect(peak(out, 1), closeTo(0.25, 0.005));

    engine.setParam(TcNode.master, TcParam.gain, 2.0);
    expect(engine.getParam(TcNode.master, TcParam.gain), 2.0);
    for (var i = 0; i < 100; i++) {
      out = engine.processOffline(input, n);
    }
    expect(peak(out, 0), closeTo(0.5, 0.01));

    final m = engine.metrics;
    expect(m.framesProcessed, n * 141);
    expect(m.callbacks, 141);
    expect(m.sampleRate, 48000);
    expect(m.monitorActive, isTrue);
    expect(m.audioApi, AudioApi.none);
  });

  test('tự chỉnh giọng: tone, preset, trạng thái cao độ', () {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();

    engine.key = const MusicKey(9, MusicScale.naturalMinor);
    expect(engine.key, const MusicKey(9, MusicScale.naturalMinor));
    engine.applyCorrectionPreset(CorrectionPreset.robot);
    expect(engine.getParam(TcNode.pitch, TcParam.retuneMs), 0.0);
    // Limiter đầu ra nhìn trước 1 ms (M5) luôn có; chỉnh giọng thêm 10 ms.
    expect(engine.metrics.algorithmLatencyFrames, 48);
    engine.pitchCorrectionEnabled = true;
    expect(engine.pitchCorrectionEnabled, isTrue);
    expect(engine.metrics.algorithmLatencyFrames, 480 + 48);
    expect(engine.metrics.algorithmLatencyMs, closeTo(11.0, 1e-9));

    // La4 cao 30 cent, sóng có hoà âm (giống giọng hơn sin thuần).
    final f = 440.0 * math.pow(2, 30 / 1200);
    final block = Float32List(256);
    var t = 0;
    for (var b = 0; b < 100; b++) {
      for (var i = 0; i < 256; i++, t++) {
        final ph = 2 * math.pi * f * t / 48000;
        block[i] =
            0.2 * math.sin(ph) +
            0.1 * math.sin(2 * ph) +
            0.05 * math.sin(3 * ph);
      }
      engine.processOffline(block, 256);
    }
    final s = engine.pitchState;
    expect(s.voiced, isTrue);
    expect(s.enabled, isTrue);
    expect(s.f0Hz, closeTo(f, 2.0));
    expect(s.targetNote, 69);
    expect(s.centsOff, closeTo(30, 5));
    expect(s.correctionCents, closeTo(-30, 5));

    expect(
      () => engine.key = const MusicKey(0, MusicScale.chromatic),
      returnsNormally,
    );
  });

  test('route Bluetooth chặn nghe lại giọng và phát sự kiện', () {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();
    engine.monitorEnabled = true;
    engine.setRouteHint(AudioRoute.bluetooth);
    final evs = engine.pollEvents();
    expect(
      evs.map((e) => e.type),
      containsAll([
        ToneCoreEventType.routeChanged,
        ToneCoreEventType.monitorBlocked,
      ]),
    );
    expect(engine.metrics.route, AudioRoute.bluetooth);
    expect(engine.metrics.monitorActive, isFalse);
  });

  test('nạp beat WAV, phát, tua và đọc trạng thái', () async {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();
    final path = '${Directory.systemTemp.path}/tonecore_dart_beat.wav';
    _writeSineWav(path, seconds: 1, sampleRate: 44100);
    addTearDown(() => File(path).deleteSync());

    final duration = await engine.loadBeatAndWait(path);
    expect(duration.inMilliseconds, closeTo(1000, 1));
    engine.play();
    for (var i = 0; i < 10; i++) {
      engine.processOffline(null, 256);
    }
    var pb = engine.playback;
    expect(pb.hasBeat, isTrue);
    expect(pb.playing, isTrue);
    expect(pb.position.inMilliseconds, closeTo(53, 2));
    engine.seek(const Duration(milliseconds: 900));
    expect(engine.playback.position.inMilliseconds, 900);
    engine.pause();
    expect(engine.playback.playing, isFalse);
  });

  test('beat không tồn tại báo lỗi', () async {
    final engine = ToneCoreEngine.create();
    addTearDown(engine.dispose);
    engine.start();
    expect(
      engine.loadBeatAndWait('/khong/co/file.mp3'),
      throwsA(isA<ToneCoreException>()),
    );
  });

  test('backend NULL chạy được đo độ trễ khi có xử lý offline', () async {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();
    // Vòng loopback giả lập trễ 960 mẫu (20 ms).
    final played = <double>[];
    final result = engine.measureLatency(timeout: const Duration(seconds: 60));
    final timer = Timer.periodic(const Duration(microseconds: 100), (_) {
      final base = played.length;
      final input = Float32List(256);
      for (var i = 0; i < 256; i++) {
        final src = base + i - 960;
        if (src >= 0 && src < base) input[i] = 0.1 * played[src];
      }
      final out = engine.processOffline(input, 256);
      for (var i = 0; i < 256; i++) {
        played.add(out[2 * i]);
      }
    });
    final r = await result;
    timer.cancel();
    expect(r.valid, isTrue);
    expect(r.latencyMs, closeTo(20.0, 0.05));
  });

  group('phân tích bài (v0.4)', () {
    late String cadence;
    late String longSine;
    setUpAll(() {
      cadence = '${Directory.systemTemp.path}/tonecore_dart_cadence.wav';
      _writeCadenceWav(cadence);
      longSine = '${Directory.systemTemp.path}/tonecore_dart_long.wav';
      _writeSineWav(longSine, seconds: 120, sampleRate: 44100);
    });
    tearDownAll(() {
      File(cadence).deleteSync();
      File(longSine).deleteSync();
    });

    test('dò tone, BPM, tuning, độ to và timeline', () async {
      final analyzer = SongAnalyzer.create();
      addTearDown(analyzer.dispose);
      expect(analyzer.status.state, AnalysisState.idle);
      final seen = <AnalysisStatus>[];
      final a = await analyzer.analyze(
        cadence,
        onProgress: seen.add,
        pollInterval: const Duration(milliseconds: 5),
      );
      expect(a.duration.inMilliseconds, closeTo(16000, 1));
      expect(a.sampleRate, 44100);
      expect(a.key, const MusicKey(2, MusicScale.major));
      expect(a.keyConfidence, greaterThan(0));
      expect(a.bpm, closeTo(120, 1.2));
      expect(a.tuningCents, closeTo(0, 3));
      expect(a.integratedLufs, lessThan(0));
      expect(a.timeline, isNotEmpty);
      expect(a.timeline.first.start, Duration.zero);
      expect(a.timeline.last.end, a.duration);
      expect(a.timeline.first.key, a.key);
      // Tiến độ chỉ tăng, kết thúc ở giai đoạn "xong".
      for (var i = 1; i < seen.length; i++) {
        expect(seen[i].progress, greaterThanOrEqualTo(seen[i - 1].progress));
      }
      final last = analyzer.status;
      expect(last.state, AnalysisState.done);
      expect(last.stage, AnalysisStage.done);
      expect(last.progress, 1.0);
    });

    test('file không tồn tại báo lỗi', () async {
      final analyzer = SongAnalyzer.create();
      addTearDown(analyzer.dispose);
      await expectLater(
        analyzer.analyze('/khong/co/file.mp3'),
        throwsA(
          isA<ToneCoreException>().having(
            (e) => e.code,
            'code',
            TcResult.invalidArg,
          ),
        ),
      );
      expect(analyzer.status.state, AnalysisState.failed);
      expect(() => analyzer.result, throwsA(isA<ToneCoreException>()));
    });

    test('huỷ giữa chừng rồi dùng lại', () async {
      final analyzer = SongAnalyzer.create();
      addTearDown(analyzer.dispose);
      final running = analyzer.analyze(
        longSine,
        pollInterval: const Duration(milliseconds: 5),
      );
      analyzer.cancel();
      try {
        await running;
      } on ToneCoreException catch (e) {
        expect(e.code, TcResult.cancelled);
        expect(analyzer.status.state, AnalysisState.cancelled);
      }
      final again = await analyzer.analyze(cadence);
      expect(again.key, const MusicKey(2, MusicScale.major));
    });

    test('dispose khi đang chạy không treo', () {
      final analyzer = SongAnalyzer.create();
      analyzer.start(longSine);
      final sw = Stopwatch()..start();
      analyzer.dispose();
      expect(sw.elapsed, lessThan(const Duration(seconds: 10)));
      expect(() => analyzer.status, throwsStateError);
      analyzer.dispose(); // gọi lần hai không sao
    });
  });

  test('tuning và key theo timeline của bài', () async {
    final engine = ToneCoreEngine.create(maxBlockFrames: 256);
    addTearDown(engine.dispose);
    engine.start();
    expect(engine.tuningCents, 0);
    engine.tuningCents = 18.5;
    expect(engine.tuningCents, closeTo(18.5, 1e-6));
    expect(() => engine.tuningCents = 500, throwsA(isA<ToneCoreException>()));

    final path = '${Directory.systemTemp.path}/tonecore_dart_tl_beat.wav';
    _writeSineWav(path, seconds: 3, sampleRate: 48000);
    addTearDown(() => File(path).deleteSync());
    await engine.loadBeatAndWait(path);
    engine.play();

    const c = MusicKey(0, MusicScale.major);
    const am = MusicKey(9, MusicScale.naturalMinor);
    engine.setKeyTimeline(const [
      KeySegment(start: Duration.zero, end: Duration(seconds: 1), key: c),
      KeySegment(
        start: Duration(seconds: 1),
        end: Duration(seconds: 3),
        key: am,
      ),
    ]);
    expect(engine.key, c);
    final changes = <MusicKey>[];
    void run(int blocks) {
      for (var i = 0; i < blocks; i++) {
        engine.processOffline(null, 256);
      }
      for (final e in engine.pollEvents()) {
        if (e.type == ToneCoreEventType.keyChanged) changes.add(e.key!);
      }
    }

    run(100); // ~0,53 s
    expect(engine.key, c);
    run(120); // qua mốc 1 s
    expect(engine.key, am);
    expect(changes, [c, am]);

    // Chọn tay → thôi theo timeline.
    engine.key = const MusicKey(5, MusicScale.major);
    engine.seek(Duration.zero);
    run(20);
    expect(engine.key, const MusicKey(5, MusicScale.major));
    expect(changes, [c, am]);

    expect(
      () => engine.setKeyTimeline(const [
        KeySegment(
          start: Duration(seconds: 2),
          end: Duration(seconds: 1),
          key: c,
        ),
      ]),
      throwsA(isA<ToneCoreException>()),
    );
    engine.clearKeyTimeline();
  });

  test(
    'đổi tone beat (v0.5): key áp dụng cộng nửa cung, sự kiện, về 0',
    () async {
      final engine = ToneCoreEngine.create(maxBlockFrames: 256);
      addTearDown(engine.dispose);
      engine.start();
      final path = '${Directory.systemTemp.path}/tonecore_dart_tr_beat.wav';
      _writeSineWav(path, seconds: 3, sampleRate: 48000);
      addTearDown(() => File(path).deleteSync());
      expect(() => engine.transpose = 2, throwsA(isA<ToneCoreException>()));
      await engine.loadBeatAndWait(path);
      engine.key = const MusicKey(0, MusicScale.major);
      engine.play();
      expect(engine.playback.transpose, 0);
      expect(engine.playback.transposeProgress, 1.0);

      engine.transpose = 3;
      expect(engine.transpose, 3);
      expect(engine.playback.transposePending, isTrue);
      final events = <ToneCoreEvent>[];
      // Chạy audio (luồng nền render song song) tới khi beat nghe ở tone mới.
      final deadline = DateTime.now().add(const Duration(seconds: 30));
      while (engine.playback.audibleTranspose != 3) {
        expect(DateTime.now().isBefore(deadline), isTrue);
        engine.processOffline(null, 256);
        events.addAll(engine.pollEvents());
        await Future<void>.delayed(const Duration(milliseconds: 1));
      }
      expect(engine.key, const MusicKey(0, MusicScale.major));
      expect(engine.appliedKey, const MusicKey(3, MusicScale.major));
      events.addAll(engine.pollEvents());
      expect(
        events.where((e) => e.type == ToneCoreEventType.keyChanged).last.key,
        const MusicKey(3, MusicScale.major),
      );
      while (engine.playback.transposeProgress < 1.0) {
        expect(DateTime.now().isBefore(deadline), isTrue);
        await Future<void>.delayed(const Duration(milliseconds: 5));
      }
      await Future<void>.delayed(const Duration(milliseconds: 20));
      events.addAll(engine.pollEvents());
      final ready = events.where(
        (e) => e.type == ToneCoreEventType.transposeReady,
      );
      expect(ready.single.code, 3);

      engine.transpose = 0;
      engine.processOffline(null, 256);
      expect(engine.playback.audibleTranspose, 0);
      expect(engine.appliedKey, const MusicKey(0, MusicScale.major));
      expect(() => engine.transpose = 9, throwsA(isA<ToneCoreException>()));
    },
  );

  test('block quá lớn bị từ chối ở phía Dart', () {
    final engine = ToneCoreEngine.create(maxBlockFrames: 64);
    addTearDown(engine.dispose);
    engine.start();
    expect(() => engine.processOffline(null, 65), throwsRangeError);
  });
}

/// Ghi WAV 16-bit stereo, sin 440 Hz.
void _writeSineWav(
  String path, {
  required int seconds,
  required int sampleRate,
}) {
  final frames = seconds * sampleRate;
  final data = ByteData(44 + frames * 4);
  void str(int o, String s) {
    for (var i = 0; i < s.length; i++) {
      data.setUint8(o + i, s.codeUnitAt(i));
    }
  }

  str(0, 'RIFF');
  data.setUint32(4, 36 + frames * 4, Endian.little);
  str(8, 'WAVEfmt ');
  data.setUint32(16, 16, Endian.little);
  data.setUint16(20, 1, Endian.little);
  data.setUint16(22, 2, Endian.little);
  data.setUint32(24, sampleRate, Endian.little);
  data.setUint32(28, sampleRate * 4, Endian.little);
  data.setUint16(32, 4, Endian.little);
  data.setUint16(34, 16, Endian.little);
  str(36, 'data');
  data.setUint32(40, frames * 4, Endian.little);
  for (var i = 0; i < frames; i++) {
    final v = (16000 * math.sin(2 * math.pi * 440 * i / sampleRate)).round();
    data.setInt16(44 + i * 4, v, Endian.little);
    data.setInt16(46 + i * 4, v, Endian.little);
  }
  File(path).writeAsBytesSync(data.buffer.asUint8List());
}

/// Vòng hoà âm Rê trưởng I–IV–V–I (2 s mỗi hợp âm, 2 vòng = 16 s) có nốt bass, cộng tiếng gõ
/// mỗi 0,5 s (120 BPM). WAV 16-bit stereo 44,1 kHz.
void _writeCadenceWav(String path) {
  const sampleRate = 44100;
  const seconds = 16;
  const frames = sampleRate * seconds;
  const chords = [
    [50, 62, 66, 69], // D
    [43, 62, 67, 71], // G
    [45, 61, 64, 69], // A
    [50, 62, 66, 69], // D
  ];
  final mix = Float64List(frames);
  double hz(int midi) => 440.0 * math.pow(2, (midi - 69) / 12);
  for (var c = 0; c < 8; c++) {
    final start = c * 2 * sampleRate;
    for (final m in chords[c % 4]) {
      for (var h = 1; h <= 4; h++) {
        final f = hz(m) * h;
        for (var i = 0; i < 2 * sampleRate; i++) {
          final env =
              math.min(1.0, i / 441) *
              math.min(1.0, (2 * sampleRate - i) / 2205);
          mix[start + i] +=
              0.05 * env * math.sin(2 * math.pi * f * i / sampleRate) / h;
        }
      }
    }
  }
  final rng = math.Random(7);
  for (var k = 0; k < 32; k++) {
    final start = k * sampleRate ~/ 2;
    for (var i = 0; i < 1300 && start + i < frames; i++) {
      mix[start + i] += 0.4 * math.exp(-i / 176.0) * (rng.nextDouble() * 2 - 1);
    }
  }
  final data = ByteData(44 + frames * 4);
  void str(int o, String s) {
    for (var i = 0; i < s.length; i++) {
      data.setUint8(o + i, s.codeUnitAt(i));
    }
  }

  str(0, 'RIFF');
  data.setUint32(4, 36 + frames * 4, Endian.little);
  str(8, 'WAVEfmt ');
  data.setUint32(16, 16, Endian.little);
  data.setUint16(20, 1, Endian.little);
  data.setUint16(22, 2, Endian.little);
  data.setUint32(24, sampleRate, Endian.little);
  data.setUint32(28, sampleRate * 4, Endian.little);
  data.setUint16(32, 4, Endian.little);
  data.setUint16(34, 16, Endian.little);
  str(36, 'data');
  data.setUint32(40, frames * 4, Endian.little);
  for (var i = 0; i < frames; i++) {
    final v = (32767 * mix[i].clamp(-1.0, 1.0)).round();
    data.setInt16(44 + i * 4, v, Endian.little);
    data.setInt16(46 + i * 4, v, Endian.little);
  }
  File(path).writeAsBytesSync(data.buffer.asUint8List());
}
