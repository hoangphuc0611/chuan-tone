// Bộ điều khiển phiên hát (M8 B2) chạy với engine thật, backend NULL (âm thanh xử lý bằng
// processOffline):
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/sing_controller_test.dart

import 'dart:io';

import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/data/song_ref.dart';
import 'package:chuantone/data/stem_store.dart';
import 'package:chuantone/data/take_store.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine/genre_rules.dart';
import 'package:chuantone/engine/sing_controller.dart';
import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';

const _dMajor = MusicKey(2, MusicScale.major);
const _aMinor = MusicKey(9, MusicScale.naturalMinor);

/// Camera giả: ghi lại lệnh, dừng quay trả một clip.
class _FakeCamera extends SessionCamera {
  final calls = <String>[];

  @override
  bool get supported => true;
  @override
  Future<bool> start() async {
    calls.add('start');
    return true;
  }

  @override
  Future<void> stop() async => calls.add('stop');
  @override
  Future<void> recordStart(String path) async => calls.add('record $path');
  @override
  Future<CameraClip?> recordStop() async {
    calls.add('recordStop');
    return const CameraClip(
      path: '/tmp/x/camera.mp4',
      firstFrameNs: 5,
      duration: Duration(seconds: 1),
      frames: 30,
      dropped: 0,
    );
  }
}

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  late ToneCoreEngine engine;
  late AudioService service;
  late SongRef song;

  setUp(() async {
    if (skip != null) return;
    dir = Directory.systemTemp.createTempSync('sing');
    final src = '${dir.path}/Bài thử Bolero.wav';
    writeCadenceWav(src);
    song = (await SongLibrary(baseDir: dir).add(src, move: true)).ref;
    service = AudioService.test(
      () => engine = ToneCoreEngine.create(maxBlockFrames: 256),
    );
  });

  tearDown(() async {
    if (skip != null) return;
    await service.shutdown();
    await LibraryDb.closeAll();
    dir.deleteSync(recursive: true);
  });

  SingController make({SessionCamera camera = const SessionCamera()}) {
    final c = SingController(
      service: service,
      baseDir: dir,
      camera: camera,
      autoTick: false,
      countdownStep: Duration.zero,
    );
    addTearDown(c.dispose);
    return c;
  }

  /// Chạy engine [blocks] khối 256 mẫu với giọng giả ở micro.
  void run(int blocks) {
    for (var i = 0; i < blocks; i++) {
      engine.processOffline(sineBlock(256, offset: i * 256), 256);
    }
  }

  test(
    'mở bài: nạp nhạc, thể loại theo tên, dò tone rồi chỉnh giọng theo bài',
    () async {
      final c = make();
      expect(c.starting, isTrue);
      expect(await c.start(initial: song), isTrue);
      expect(c.starting, isFalse);
      expect(c.song?.name, 'Bài thử Bolero.wav');
      expect(c.title, 'Bài thử Bolero');
      expect(c.playback?.hasBeat, isTrue);
      expect(c.playback?.duration.inSeconds, 16);
      expect(service.genre, Genre.bolero);
      expect(c.genreSource, GenreSource.name);
      expect(c.canRecord, isTrue);

      await c.analysisDone;
      expect(c.analyzing, isFalse);
      expect(c.analysisError, isNull);
      expect(c.analysis?.key, _dMajor);
      expect(c.analysis?.bpm, closeTo(120, 1.5));
      expect(c.keySource, KeySource.song);
      expect(c.currentKey, _dMajor);
      expect(c.analysisTime, isNotNull);

      // Mở lại: kết quả lấy từ bộ nhớ (không phân tích lại).
      await c.openSong(song);
      await c.analysisDone;
      expect(c.analysis?.key, _dMajor);
      expect(c.analysisTime, isNull);
    },
    skip: skip,
  );

  test('tone nhạc + thể loại nhớ theo bài, phiên sau khôi phục', () async {
    final c = make();
    await c.start(initial: song);
    await c.analysisDone;
    c.setTranspose(2);
    expect(engine.transpose, 2);
    expect(c.playback?.transpose, 2);
    c.pickGenre(Genre.ballad);
    expect(service.genre, Genre.ballad);
    expect(c.genreSource, GenreSource.saved);
    c.setTranspose(9); // kẹp ±6
    expect(engine.transpose, 6);
    c.setTranspose(-1);
    // Ghi nền: chờ cho chắc đã xuống CSDL.
    await Future<void>.delayed(const Duration(milliseconds: 50));
    final prefs = SongPrefs(baseDir: dir);
    expect(await prefs.transposeFor(name: song.name, size: song.size), -1);
    expect(
      await prefs.genreFor(name: song.name, size: song.size),
      Genre.ballad,
    );
    expect(await prefs.lastGenre(), Genre.ballad);
    c.dispose();

    // Mở app lại: engine mới, thể loại chọn gần nhất + tone nhạc của bài.
    await service.shutdown();
    final s2 = AudioService.test(
      () => engine = ToneCoreEngine.create(maxBlockFrames: 256),
    );
    addTearDown(s2.shutdown);
    final c2 = SingController(
      service: s2,
      baseDir: dir,
      autoTick: false,
      countdownStep: Duration.zero,
    );
    addTearDown(c2.dispose);
    await c2.start(initial: song);
    expect(engine.transpose, -1);
    expect(s2.genre, Genre.ballad);
    expect(c2.genreSource, GenreSource.saved);
  }, skip: skip);

  test(
    'sửa tone tay: nhớ theo bài, thắng kết quả dò; bỏ sửa thì theo bài',
    () async {
      final c = make();
      await c.start(initial: song);
      await c.analysisDone;
      await c.setManualKey(_aMinor);
      expect(c.keySource, KeySource.manual);
      expect(c.manualKey, _aMinor);
      expect(c.currentKey, _aMinor);
      expect(service.followSong, isFalse);

      await c.openSong(song);
      await c.analysisDone;
      expect(c.keySource, KeySource.manual);
      expect(engine.key, _aMinor);

      await c.setManualKey(null);
      expect(c.keySource, KeySource.song);
      expect(c.currentKey, _dMajor);
      expect(
        await SongPrefs(baseDir: dir).keyFor(name: song.name, size: song.size),
        isNull,
      );

      // Chọn tay trong phiên (Phòng thử): không lưu theo bài.
      c.setKey(_aMinor);
      expect(c.keySource, KeySource.picked);
      c.setFollowSong(true);
      expect(c.keySource, KeySource.song);
    },
    skip: skip,
  );

  test('thu âm: đếm ngược khi nhạc dừng, khoá tone, dừng → lưu → Nghe lại → nạp lại bài', () async {
    final camera = _FakeCamera();
    final c = make(camera: camera);
    await c.start(initial: song);
    await c.analysisDone;
    c.setTranspose(1);
    // Chờ bản đổi tone nghe được (render ở luồng nền): bản thu ghi tone đang nghe.
    final deadline = DateTime.now().add(const Duration(seconds: 30));
    while (engine.playback.audibleTranspose != 1 &&
        DateTime.now().isBefore(deadline)) {
      run(1);
      await Future<void>.delayed(const Duration(milliseconds: 1));
    }
    expect(engine.playback.audibleTranspose, 1);
    await c.setCamera(true);
    expect(c.cameraOn, isTrue);

    final counts = <int>[];
    c.addListener(() {
      final n = c.countdown;
      if (n != null && (counts.isEmpty || counts.last != n)) counts.add(n);
    });
    await c.startRecording();
    expect(counts, [3, 2, 1]);
    expect(c.recording, isTrue);
    expect(c.countdown, isNull);
    expect(c.playback?.playing ?? engine.playback.playing, isTrue);
    expect(engine.playback.playing, isTrue);
    expect(camera.calls.last, startsWith('record ${dir.path}/takes/'));

    // Khoá tone khi thu.
    c.setTranspose(3);
    expect(engine.transpose, 1);

    run(40);
    c.refresh();
    expect(c.recStatus?.frames, 40 * 256);

    Take? reviewed;
    c.onTake = (t) async => reviewed = t;
    final take = await c.stopRecording();
    expect(take, isNotNull);
    expect(reviewed?.dir.path, take!.dir.path);
    expect(c.recording, isFalse);
    expect(camera.calls, contains('recordStop'));
    expect(take.info.songName, song.name);
    expect(take.info.frames, 40 * 256);
    expect(take.info.genre, Genre.bolero);
    expect(take.info.transpose, 1);
    expect(take.info.videoFile, 'camera.mp4');
    expect(File('${take.dir.path}/take.json').existsSync(), isTrue);
    expect(await TakeStore(baseDir: dir).list(), hasLength(1));
    // Sau màn Nghe lại: bài nạp lại, tone nhạc giữ, nhạc dừng.
    expect(engine.playback.hasBeat, isTrue);
    expect(engine.playback.hasTake, isFalse);
    expect(engine.transpose, 1);
    expect(engine.playback.playing, isFalse);
  }, skip: skip);

  test('nhạc đang phát thì thu ngay, không đếm ngược', () async {
    final c = make();
    await c.start(initial: song);
    c.playPause();
    expect(engine.playback.playing, isTrue);
    final counts = <int?>[];
    c.addListener(() => counts.add(c.countdown));
    await c.startRecording();
    expect(c.recording, isTrue);
    expect(counts.whereType<int>(), isEmpty);
    // Rời màn khi đang thu: dừng ghi, dừng nhạc.
    c.dispose();
    expect(engine.recordStatus.recording, isFalse);
    expect(engine.playback.playing, isFalse);
  }, skip: skip);

  test(
    'phát / tua: tua lùi 10 s không âm, tua tới không quá cuối bài',
    () async {
      final c = make();
      await c.start(initial: song);
      c.seek(const Duration(seconds: 5));
      c.skip(const Duration(seconds: -10));
      expect(engine.playback.position, Duration.zero);
      c.skip(const Duration(seconds: 30));
      expect(engine.playback.position.inSeconds, 16);
      c.playPause();
      expect(c.playback?.playing, isTrue);
      c.playPause();
      expect(c.playback?.playing, isFalse);
    },
    skip: skip,
  );

  test('âm lượng, nghe lại giọng, Vang, giữ giọng ca sĩ', () async {
    final c = make();
    await c.start(initial: song);
    c.setBeatGain(0.5);
    expect(c.beatGain, 0.5);
    c.setMonitorGain(3);
    expect(c.monitorGain, 2);
    expect(engine.monitorGain, closeTo(2, 1e-6));
    c.setMonitorOn(true);
    expect(engine.monitorEnabled, isTrue);
    c.setFxMix(1.5);
    expect(service.fxMix, 1.5);
    c.setCorrection(CorrectionPreset.robot);
    expect(service.correction, CorrectionPreset.robot);
    expect(engine.pitchCorrectionEnabled, isTrue);
    c.setCorrection(null);
    expect(engine.pitchCorrectionEnabled, isFalse);
  }, skip: skip);

  test('bài hỏng: báo lỗi, giữ bài đang hát', () async {
    final c = make();
    await c.start(initial: song);
    final id = c.messageId;
    await c.openSong(
      SongRef(path: '${dir.path}/khong-co.mp3', name: 'khong-co.mp3', size: 1),
    );
    expect(c.message, contains('Không mở được file'));
    expect(c.messageId, id + 1);
    expect(c.song?.name, song.name);
    expect(c.loading, isFalse);
    c.clearMessage();
    expect(c.message, isNull);
  }, skip: skip);

  test('bản tách giọng hỏng → xoá, nạp bài thường', () async {
    final stems = StemStore(baseDir: dir);
    final d = await stems.prepare(name: song.name, size: song.size);
    File('${d.path}/${StemStore.chunkName(0)}').writeAsStringSync('hỏng');
    await stems.markDone(name: song.name, size: song.size, chunks: 1);
    expect(await stems.vocalsFor(name: song.name, size: song.size), isNotNull);

    final c = make();
    await c.start(initial: song);
    expect(c.hasStems, isFalse);
    expect(c.playback?.hasBeat, isTrue);
    expect(await stems.vocalsFor(name: song.name, size: song.size), isNull);
  }, skip: skip);

  test(
    'lời bài hát: nạp theo bài, vị trí phát, lệch lời nhớ theo bài',
    () async {
      final lrc = File('${dir.path}/loi.lrc')
        ..writeAsStringSync('[00:01.00]Câu một\n[00:05.00]Câu hai\n');
      await SongLibrary(baseDir: dir).setLyrics(song.name, song.size, lrc.path);
      final c = make();
      await c.start(initial: song);
      expect(c.lyrics?.lines, hasLength(2));
      expect(c.lyricsOffsetMs, 0);

      c.seek(const Duration(seconds: 6));
      expect(c.position.value, const Duration(seconds: 6));
      c.playPause();
      run(200); // ~1 s ở 48 kHz
      c.pitchTick();
      expect(c.position.value.inMilliseconds, closeTo(7067, 30));

      c.nudgeLyrics(500);
      c.nudgeLyrics(500);
      expect(c.lyricsOffsetMs, 1000);
      c.nudgeLyrics(-20000);
      expect(c.lyricsOffsetMs, -10000);
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(
        (await SongLibrary(
          baseDir: dir,
        ).lyricsFor(song.name, song.size))!.offsetMs,
        -10000,
      );

      final bad = File('${dir.path}/x.txt')..writeAsStringSync('không có mốc');
      await expectLater(c.setLyricsFile(bad.path), throwsFormatException);
      expect(c.lyrics?.lines, hasLength(2));

      await c.removeLyrics();
      expect(c.lyrics, isNull);
      await c.openSong(song);
      expect(c.lyrics, isNull);
      await c.setLyricsFile(lrc.path);
      expect(c.lyrics?.lines.first.text, 'Câu một');
    },
    skip: skip,
  );

  test('không mở được thiết bị âm thanh → false + lỗi', () async {
    final bad = AudioService.test(() => ToneCoreEngine.create(sampleRate: 10));
    final c = SingController(service: bad, baseDir: dir, autoTick: false);
    addTearDown(c.dispose);
    expect(await c.start(initial: song), isFalse);
    expect(c.engine, isNull);
    expect(c.starting, isFalse);
    expect(c.startError, contains('Không mở được thiết bị âm thanh'));
    // Lệnh khi chưa có engine: không làm gì, không lỗi.
    c.setTranspose(1);
    c.playPause();
    await c.startRecording();
    expect(c.recording, isFalse);
  }, skip: skip);
}
