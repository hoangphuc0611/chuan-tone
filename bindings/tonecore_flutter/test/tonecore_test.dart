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
    expect(ToneCore.apiVersionString, '0.2.0');
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

    final input = Float32List(256)..fillRange(0, 256, 0.5);
    var out = engine.processOffline(input, 256);
    expect(out.length, 512);
    expect(out[0], 0.0); // nghe lại giọng mặc định tắt

    engine.monitorEnabled = true;
    expect(engine.monitorEnabled, isTrue);
    for (var i = 0; i < 40; i++) {
      out = engine.processOffline(input, 256);
    }
    expect(out[0], closeTo(0.5, 1e-4));
    expect(out[1], closeTo(0.5, 1e-4));

    engine.setParam(TcNode.master, TcParam.gain, 2.0);
    expect(engine.getParam(TcNode.master, TcParam.gain), 2.0);
    for (var i = 0; i < 100; i++) {
      out = engine.processOffline(input, 256);
    }
    expect(out[510], closeTo(1.0, 1e-4));

    final m = engine.metrics;
    expect(m.framesProcessed, 256 * 141);
    expect(m.callbacks, 141);
    expect(m.sampleRate, 48000);
    expect(m.monitorActive, isTrue);
    expect(m.audioApi, AudioApi.none);
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
