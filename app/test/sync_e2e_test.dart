// Kiểm tra khớp tiếng end-to-end với engine thật (backend NULL): micro giả = đầu ra trễ D frame
// (như loa → micro), bản thu phải cho giọng khô chậm beat đúng D + limiter nhìn trước. Chạy khi có
// thư viện engine:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/sync_e2e_test.dart

import 'dart:io';
import 'dart:typed_data';

import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/sync_check.dart';
import 'package:chuantone/engine/take_alignment.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  final ready = (Platform.environment['TONECORE_LIB'] ?? '').isNotEmpty;

  test('bản thu: giọng khô chậm beat = độ trễ khứ hồi + limiter', () async {
    final dir = Directory.systemTemp.createTempSync('sync_e2e');
    addTearDown(() => dir.deleteSync(recursive: true));
    const rate = 48000;
    const block = 256;
    const delay = 600; // "độ trễ khứ hồi" giả: 12,5 ms
    final mono = clickTrack(length: const Duration(seconds: 4));
    final stereo = Float32List(mono.length * 2);
    for (var i = 0; i < mono.length; i++) {
      stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    }
    final beatPath = '${dir.path}/tach.wav';
    File(beatPath)
        .writeAsBytesSync(encodeWav16(stereo, sampleRate: rate, channels: 2));

    final e = ToneCoreEngine.create(maxBlockFrames: block);
    addTearDown(e.dispose);
    e.start();
    await e.loadBeatAndWait(beatPath);
    e.play();
    e.startRecording(dir.path);
    final out = <double>[];
    for (var b = 0; b < 3 * rate ~/ block; b++) {
      final mic = Float32List(block);
      for (var i = 0; i < block; i++) {
        final n = b * block + i - delay;
        mic[i] = n >= 0 ? 0.3 * out[2 * n] : 0;
      }
      out.addAll(e.processOffline(mic, block));
    }
    e.stopRecording();
    final s = e.recordStatus;
    final beat = readWavFile('${dir.path}/beat.wav').channel(0);
    final dry = readWavFile('${dir.path}/dry.wav').channel(0);
    final m = measureSync(beat: beat, dry: dry, sampleRate: rate)!;
    final want = voiceOffsetFrames(
      roundTripMs: delay * 1000 / rate,
      sampleRate: rate,
      lookaheadFrames: s.outputLookaheadFrames,
    );
    expect(want, delay + 48);
    expect(m.pairs, greaterThanOrEqualTo(5));
    expect((m.lagFrames - want).abs(), lessThanOrEqualTo(2));
  }, skip: ready ? false : 'cần TONECORE_LIB');
}
