// Test binding FFI trên máy dev / CI: nạp thư viện host qua biến môi trường TONECORE_LIB.
//   TONECORE_LIB=../../engine/build/host/libtonecore.dylib fvm flutter test

import 'dart:io';
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
    expect(ToneCore.apiVersionString, '0.1.0');
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
    expect(out[0], closeTo(0.5, 1e-6));
    expect(out[1], closeTo(0.5, 1e-6));

    engine.setParam(TcNode.master, TcParam.gain, 2.0);
    expect(engine.getParam(TcNode.master, TcParam.gain), 2.0);
    for (var i = 0; i < 100; i++) {
      out = engine.processOffline(input, 256);
    }
    expect(out[510], closeTo(1.0, 1e-6));

    final m = engine.metrics;
    expect(m.framesProcessed, 256 * 101);
    expect(m.callbacks, 101);
    expect(m.sampleRate, 48000);
  });

  test('block quá lớn bị từ chối ở phía Dart', () {
    final engine = ToneCoreEngine.create(maxBlockFrames: 64);
    addTearDown(engine.dispose);
    engine.start();
    expect(() => engine.processOffline(null, 65), throwsRangeError);
  });
}
