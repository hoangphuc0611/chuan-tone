// Chế độ soundcard (M8 B6) với engine thật, backend NULL — "soundcard" giả: route USB, Loopback =
// micro nhận lại tiếng ra của điện thoại sau D frame:
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/soundcard_test.dart

import 'dart:io';
import 'dart:typed_data';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/latency_history.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_ref.dart';
import 'package:chuantone/data/take_store.dart';
import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/beat_leak.dart';
import 'package:chuantone/engine/latency_measure.dart';
import 'package:chuantone/engine/take_alignment.dart';
import 'package:chuantone/engine/takes.dart';
import 'package:chuantone/pages/review_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'beat_leak_test.dart' show fakeBeat, fakeVoice;
import 'helpers/engine_audio.dart';

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('soundcard'));
  tearDown(() => dir.deleteSync(recursive: true));
  tearDownAll(LibraryDb.closeAll);

  const rate = 48000;
  const block = 256;
  ToneCoreEngine make() => ToneCoreEngine.create(maxBlockFrames: block);

  /// Chờ sự kiện engine tới AudioService (engine hỏi sự kiện mỗi ~16 ms).
  Future<void> events() =>
      Future<void>.delayed(const Duration(milliseconds: 80));

  test('chế độ soundcard: cắm USB thì tắt nghe lại giọng, bản thu để Khô; tắt chế độ thì như tai nghe', () async {
    final settings = AppSettings(baseDir: dir);
    final s = AudioService.test(make, settings: settings);
    addTearDown(s.shutdown);
    expect(await s.ensureStarted(), isTrue);
    final e = s.engine!;
    s.setGenre(Genre.ballad);

    // Chưa chọn: cắm USB → hỏi (một lần), tạm coi như tai nghe.
    e.setRouteHint(AudioRoute.usb);
    await events();
    expect(s.route, AudioRoute.usb);
    expect(s.shouldAskSoundcard, isTrue);
    expect(e.monitorEnabled, isTrue);
    s.markSoundcardAsked();
    expect(s.shouldAskSoundcard, isFalse);

    // Chọn chế độ soundcard (lưu do người gọi làm).
    await settings.setSoundcardMode(true);
    s.updateSoundcard(mode: true);
    expect(s.soundcardActive, isTrue);
    expect(e.monitorEnabled, isFalse);
    expect(s.takeGenre, Genre.dry); // giữ vang của soundcard
    s.updateSoundcard(fx: SoundcardFx.app);
    expect(s.takeGenre, Genre.ballad);

    // Rút ra → loa ngoài: hết chế độ soundcard; cắm lại → vẫn tắt nghe lại giọng.
    e.setRouteHint(AudioRoute.speaker);
    await events();
    expect(s.soundcardActive, isFalse);
    expect(s.takeGenre, Genre.ballad);
    e.setRouteHint(AudioRoute.usb);
    await events();
    expect(e.monitorEnabled, isFalse);

    // Tắt chế độ: USB như tai nghe (tự bật nghe lại giọng).
    s.updateSoundcard(mode: false);
    expect(e.monitorEnabled, isTrue);
    expect(s.shouldAskSoundcard, isFalse);

    // Mở lại app: đọc chế độ đã lưu.
    final s2 = AudioService.test(make, settings: settings);
    addTearDown(s2.shutdown);
    await s2.ensureStarted();
    expect(s2.soundcardMode, isTrue);
    expect(s2.soundcardFx, SoundcardFx.soundcard);
  }, skip: skip);

  test('đo độ trễ qua Loopback: lưu theo route USB, bản thu qua USB dùng đúng số đo', () async {
    const delay = 1900; // ≈ 39,6 ms
    final e = make();
    addTearDown(e.dispose);
    e.start();
    e.setRouteHint(AudioRoute.usb);
    final store = LatencyHistory(baseDir: dir);
    var finished = false;
    final measuring = measureAndSaveLatency(
      e,
      store: store,
      history: const [],
    ).whenComplete(() => finished = true);
    // Loopback của soundcard: tín hiệu điện, nhỏ (−20 dB), không tiếng ồn phòng.
    final out = <double>[];
    for (var b = 0; b < 4000 && !finished; b++) {
      final mic = Float32List(block);
      for (var i = 0; i < block; i++) {
        final n = b * block + i - delay;
        mic[i] = n >= 0 ? 0.1 * out[2 * n] : 0;
      }
      out.addAll(e.processOffline(mic, block));
      if (b % 16 == 0) await Future<void>.delayed(Duration.zero);
      if (b % 64 == 0) {
        await Future<void>.delayed(const Duration(milliseconds: 2));
      }
    }
    final (r, history) = await measuring;
    expect(r.valid, isTrue);
    expect(r.route, AudioRoute.usb);
    expect(r.latencyFrames, closeTo(delay, 2));
    final rt = roundTripFor(
      route: AudioRoute.usb,
      reportedMs: 5,
      history: parseLatencyHistory(history),
    );
    expect(rt.source, LatencySource.measured);
    expect(rt.ms, closeTo(delay * 1000 / rate, 0.1));
  }, skip: skip);

  /// Thu ~10 s vào [out] với micro = giọng giả (+ tiếng ra của điện thoại trễ [delay] frame, nhân
  /// [loopGain], nếu [loopback] — soundcard trộn ngang mức micro); trả độ lệch dự kiến (trễ khứ hồi
  /// + limiter).
  Future<int> record(
    String out, {
    required bool loopback,
    double loopGain = 1.0,
  }) async {
    const delay = 1900;
    const frames = rate * 12;
    final beat = fakeBeat(frames);
    final stereo = Float32List(frames * 2);
    for (var i = 0; i < frames; i++) {
      stereo[2 * i] = stereo[2 * i + 1] = 0.5 * beat[i];
    }
    final beatPath = '${dir.path}/beat-src.wav';
    File(beatPath)
        .writeAsBytesSync(encodeWav16(stereo, sampleRate: rate, channels: 2));
    final voice = fakeVoice(frames);

    final e = make();
    addTearDown(e.dispose);
    e.start();
    await e.loadBeatAndWait(beatPath);
    e.monitorEnabled = false; // chế độ soundcard
    Directory(out).createSync();
    e.startRecording(out);
    e.play();
    final played = <double>[];
    for (var b = 0; b < 10 * rate ~/ block; b++) {
      final mic = Float32List(block);
      for (var i = 0; i < block; i++) {
        final t = b * block + i;
        final n = t - delay;
        mic[i] = voice[t] + (loopback && n >= 0 ? loopGain * played[2 * n] : 0);
      }
      played.addAll(e.processOffline(mic, block));
    }
    e.stopRecording();
    return voiceOffsetFrames(
      roundTripMs: delay * 1000 / rate,
      sampleRate: rate,
      lookaheadFrames: e.recordStatus.outputLookaheadFrames,
    );
  }

  test('bản thu: Loopback bật → báo nhạc lọt vào micro; tắt → không', () async {
    final on = '${dir.path}/loop-on';
    final lag = await record(on, loopback: true);
    final leak = checkLeakFiles(
      beatPath: '$on/beat.wav',
      dryPath: '$on/dry.wav',
      expectedLagFrames: lag,
    )!;
    expect(leak.leaked, isTrue, reason: '$leak');
    expect((leak.lagFrames - lag).abs(), lessThanOrEqualTo(24));

    final off = '${dir.path}/loop-off';
    await record(off, loopback: false);
    final clean = checkLeakFiles(
      beatPath: '$off/beat.wav',
      dryPath: '$off/dry.wav',
      expectedLagFrames: lag,
    )!;
    expect(clean.leaked, isFalse, reason: '$clean');
  }, skip: skip);

  testWidgets(
    'thu qua soundcard bật Loopback: bản thu để Khô, màn Nghe lại báo nhạc lọt vào micro',
    (tester) async {
      tester.view.physicalSize = const Size(800, 2400);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      const delay = 1900;
      final service = AudioService.test(make);
      final take = await tester.runAsync(() async {
        await service.ensureStarted();
        final e = service.engine!;
        service.setGenre(Genre.ballad);
        e.setRouteHint(AudioRoute.usb);
        await events();
        service.updateSoundcard(mode: true);
        const frames = rate * 12;
        final beat = fakeBeat(frames);
        final stereo = Float32List(frames * 2);
        for (var i = 0; i < frames; i++) {
          stereo[2 * i] = stereo[2 * i + 1] = 0.5 * beat[i];
        }
        final beatPath = '${dir.path}/bai.wav';
        File(
          beatPath,
        ).writeAsBytesSync(encodeWav16(stereo, sampleRate: rate, channels: 2));
        await e.loadBeatAndWait(beatPath);
        final session = TakeSession(
          store: TakeStore(baseDir: dir),
          latency: LatencyHistory(baseDir: dir),
        );
        await session.start(
          e,
          TakeSnapshot.of(service, e, voiceGain: 1, beatGain: 0.8),
        );
        e.play();
        final voice = fakeVoice(frames);
        final played = <double>[];
        for (var b = 0; b < 8 * rate ~/ block; b++) {
          final mic = Float32List(block);
          for (var i = 0; i < block; i++) {
            final t = b * block + i;
            final n = t - delay;
            mic[i] = voice[t] + (n >= 0 ? played[2 * n] : 0);
          }
          played.addAll(e.processOffline(mic, block));
        }
        return session.stop(
          e,
          SongRef(path: beatPath, name: 'bai.wav', size: 1),
        );
      });
      expect(take!.info.route, AudioRoute.usb);
      expect(take.info.genre, Genre.dry); // giữ vang của soundcard

      await tester.pumpWidget(
        MaterialApp(
          home: ReviewPage(
            take: take,
            store: TakeStore(baseDir: dir),
            service: service,
          ),
        ),
      );
      for (
        var i = 0;
        i < 400 &&
            find.byKey(const ValueKey('review-beat-leak')).evaluate().isEmpty;
        i++
      ) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 10)),
        );
        await tester.pump(const Duration(milliseconds: 20));
      }
      expect(find.text('Soundcard đang trộn nhạc vào micro'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(service.shutdown);
    },
    skip: skip != null,
  );
}
