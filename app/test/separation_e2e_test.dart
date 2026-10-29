// Tách giọng end-to-end trên máy dev: FlacExporter (engine native) → server thật → tải giọng →
// nạp lại bài kèm giọng. Chỉ chạy khi có server và thư viện engine:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib CHUANTONE_SERVER=http://127.0.0.1:8000 \
//   CHUANTONE_SONG=/duong/dan/bai.wav fvm flutter test test/separation_e2e_test.dart

import 'dart:io';

import 'package:chuantone/engine/native_chunk_source.dart';
import 'package:chuantone/engine/separation_client.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  final env = Platform.environment;
  final server = env['CHUANTONE_SERVER'];
  final song = env['CHUANTONE_SONG'];
  final ready =
      server != null && song != null && (env['TONECORE_LIB'] ?? '').isNotEmpty;

  test(
    'bài thật qua server thật',
    () async {
      final tmp = Directory.systemTemp.createTempSync('e2e_up');
      final out = Directory.systemTemp.createTempSync('e2e_vocals');
      addTearDown(() {
        tmp.deleteSync(recursive: true);
        out.deleteSync(recursive: true);
      });
      final source = NativeChunkSource(song!, tmp);
      addTearDown(source.dispose);
      final client = SeparationClient(baseUrl: server!, deviceId: 'e2e-test');
      addTearDown(client.close);
      SeparationProgress? last;
      final r = await client
          .separate(source, out, onProgress: (p) => last = p)
          .timeout(const Duration(minutes: 3));
      // ignore: avoid_print
      print(
        '${r.vocals.length} đoạn, xong sau ${r.elapsed.inMilliseconds} ms, '
        'server tách ${r.serverSeconds} s',
      );
      expect(last!.stage, SeparationStage.done);

      final engine = ToneCoreEngine.create(maxBlockFrames: 256);
      addTearDown(engine.dispose);
      engine.start();
      await engine.loadBeatWithVocalsAndWait(song, [
        for (final f in r.vocals) f.path,
      ]);
      expect(engine.playback.hasVocal, isTrue);
    },
    timeout: const Timeout(Duration(minutes: 3)),
    skip: ready ? false : 'cần CHUANTONE_SERVER, CHUANTONE_SONG, TONECORE_LIB',
  );
}
