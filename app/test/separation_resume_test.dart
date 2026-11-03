// Tiếp tục tách giọng dở (M8 B7) với engine thật (FlacExporter, nạp bài kèm giọng) + máy chủ giả:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/separation_resume_test.dart

import 'dart:io';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/stem_store.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/sing_controller.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';
import 'separation_test.dart' show FakeServer;

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  late FakeServer server;
  late AudioService service;

  setUp(() async {
    dir = Directory.systemTemp.createTempSync('sep_resume');
    // Giọng = đúng đoạn FLAC đã gửi (FLAC thật nên engine nạp được); chưa trả giọng cho tới khi
    // test cho phép.
    server = FakeServer()
      ..echoVocals = true
      ..holdVocals = true;
    await server.start();
    service = AudioService.test(
      () => ToneCoreEngine.create(maxBlockFrames: 256),
    );
  });

  tearDown(() async {
    await service.shutdown();
    await server.stop();
    await LibraryDb.closeAll();
    dir.deleteSync(recursive: true);
  });

  Future<void> until(bool Function() done) async {
    for (var i = 0; i < 500 && !done(); i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }
    expect(done(), isTrue, reason: 'hết giờ chờ');
  }

  /// Bài thử trong thư viện, máy chủ + đồng ý gửi bài đã đặt.
  Future<SingController Function()> prepare() async {
    final wav = '${dir.path}/Bai thu.wav';
    writeCadenceWav(wav);
    final settings = AppSettings(baseDir: dir);
    await settings.setSeparationServer(server.url);
    await settings.setSeparationConsent(true);
    await SongLibrary(baseDir: dir).add(wav);
    return () =>
        SingController(service: service, baseDir: dir, autoTick: false);
  }

  test('rời màn khi đang tách → tạm dừng, giữ job; mở lại bài → "Tiếp tục" → tách xong', () async {
    final make = await prepare();
    final song = (await SongLibrary(baseDir: dir).songs()).single.ref;

    final c1 = make();
    await c1.start(initial: song);
    final running = c1.separate(askConsent: () async => fail('đã đồng ý'));
    await until(() => server.uploaded.isNotEmpty);
    c1.dispose(); // rời Màn hát
    await running;
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, isEmpty);
    final pending = await StemStore(baseDir: dir)
        .pendingFor(name: song.name, size: song.size);
    expect(pending?.job.id, 'job1');
    expect(pending?.server, server.url);

    final c2 = make();
    addTearDown(c2.dispose);
    await c2.start(initial: song);
    expect(c2.sepPending, isNotNull);
    expect(c2.hasStems, isFalse);
    server.holdVocals = false;
    await c2.resumeSeparation();
    expect(c2.sepError, isNull);
    expect(c2.hasStems, isTrue);
    expect(c2.sepPending, isNull);
    expect(server.puts, [0]); // đoạn đã gửi không gửi lại
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, ['job1']);
  }, skip: skip);

  test(
    'mất kết nối khi đang tách → báo "bị ngắt"; quay lại app thì tự tách tiếp',
    () async {
      final make = await prepare();
      final song = (await SongLibrary(baseDir: dir).songs()).single.ref;
      final c = make();
      addTearDown(c.dispose);
      await c.start(initial: song);
      final port = server.port;
      final running = c.separate(askConsent: () async => fail('đã đồng ý'));
      await until(() => server.uploaded.isNotEmpty);
      await server.stop(); // mất Wi-Fi / iOS dừng app ở nền
      await running;
      expect(c.sepPending, isNotNull);
      expect(c.sepError, contains('bị ngắt'));

      await server.start(port: port);
      server.holdVocals = false;
      c.onAppResumed();
      await until(() => c.hasStems);
      expect(c.sepPending, isNull);
    },
    skip: skip,
    timeout: const Timeout(Duration(minutes: 1)),
  );

  test('bỏ phần tách dở: xoá job trên máy chủ và phần đã tải', () async {
    final make = await prepare();
    final song = (await SongLibrary(baseDir: dir).songs()).single.ref;
    final c1 = make();
    await c1.start(initial: song);
    final running = c1.separate(askConsent: () async => fail('đã đồng ý'));
    await until(() => server.uploaded.isNotEmpty);
    c1.dispose();
    await running;

    final c2 = make();
    addTearDown(c2.dispose);
    await c2.start(initial: song);
    expect(c2.sepPending, isNotNull);
    await c2.discardSeparation();
    expect(c2.sepPending, isNull);
    expect(server.deleted, ['job1']);
    expect(
      await StemStore(baseDir: dir)
          .pendingFor(name: song.name, size: song.size),
      isNull,
    );
  }, skip: skip);
}
