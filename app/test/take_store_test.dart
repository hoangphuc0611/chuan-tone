import 'dart:convert';
import 'dart:io';

import 'package:chuantone/data/take_store.dart';
import 'package:chuantone_media/chuantone_media.dart';
import 'package:chuantone/engine/labels.dart';
import 'package:chuantone/engine/take_alignment.dart';
import 'package:chuantone/engine/takes.dart';
import 'package:chuantone/data/song_ref.dart';
import 'package:chuantone/pages/takes_page.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

const _am = MusicKey(9, MusicScale.naturalMinor);
const _c = MusicKey(0, MusicScale.major);

TakeInfo _info({DateTime? at, int frames = 48000 * 30}) => TakeInfo(
  songName: 'Lạc trôi.m4a',
  songSize: 123,
  createdAt: at ?? DateTime(2026, 11, 2, 21, 5),
  sampleRate: 48000,
  frames: frames,
  startBeatFrame: 48000 * 10,
  outputLookaheadFrames: 48,
  route: AudioRoute.wired,
  roundTripMs: 12.5,
  latencySource: LatencySource.corrected,
  genre: Genre.bolero,
  correction: CorrectionPreset.natural,
  retuneMs: 45,
  key: _am,
  timeline: const [
    KeySegment(start: Duration.zero, end: Duration(seconds: 20), key: _am),
    KeySegment(
      start: Duration(seconds: 20),
      end: Duration(seconds: 30),
      key: _c,
    ),
  ],
  tuningCents: -3,
  tempoBpm: 68,
  fxMix: 1.2,
  transpose: 2,
);

void main() {
  group('thông tin bản thu', () {
    test('JSON đi về giữ nguyên', () {
      final a = _info();
      final b = TakeInfo.fromJson(
        jsonDecode(jsonEncode(a.toJson())) as Map<String, dynamic>,
      );
      expect(b.toJson(), a.toJson());
      expect(b.correction, CorrectionPreset.natural);
      expect(b.timeline[1].key, _c);
      expect(b.duration, const Duration(seconds: 30));
    });

    test('JSON thiếu trường bắt buộc → lỗi; trường lạ → mặc định', () {
      expect(
        () => TakeInfo.fromJson({'sampleRate': 48000, 'frames': 1}),
        throwsFormatException,
      );
      final j = _info().toJson()
        ..['genre'] = 'khong-co'
        ..['correction'] = null
        ..['route'] = 42;
      final t = TakeInfo.fromJson(j);
      expect(t.genre, Genre.dry);
      expect(t.correction, isNull);
      expect(t.route, AudioRoute.unknown);
    });

    test('độ lệch giọng = độ trễ + chỉnh tay + limiter', () {
      final a = _info();
      expect(a.voiceOffsetFrames, 600 + 48);
      final b = a.copyWith(nudgeMs: 10);
      expect(b.voiceOffsetFrames, 1080 + 48);
      final c = b.copyWith(correction: () => null, trimStartFrame: 5);
      expect(c.correction, isNull);
      expect(c.trimStartFrame, 5);
      expect(c.nudgeMs, 10);
      expect(c.retuneMs, 45);
    });

    test('chuyển tone của bài → theo thời gian bản thu, cộng tone đổi', () {
      final song = const [
        KeySegment(start: Duration.zero, end: Duration(seconds: 8), key: _am),
        KeySegment(
          start: Duration(seconds: 8),
          end: Duration(seconds: 60),
          key: _c,
        ),
        KeySegment(
          start: Duration(seconds: 60),
          end: Duration(seconds: 90),
          key: _am,
        ),
      ];
      final t = takeTimeline(song, startMs: 10000, transpose: 2);
      expect(t, hasLength(2)); // đoạn 0–8 s đã qua
      expect(t[0].start, Duration.zero);
      expect(t[0].end, const Duration(seconds: 50));
      expect(t[0].key, const MusicKey(2, MusicScale.major));
      expect(t[1].start, const Duration(seconds: 50));
      expect(t[1].key, const MusicKey(11, MusicScale.naturalMinor));
    });
  });

  group('nhãn bản thu', () {
    test('tên bài, dung lượng, ngày, giọng sớm / muộn, màu thể loại', () {
      expect(songTitle('Lạc trôi.m4a'), 'Lạc trôi');
      expect(songTitle('.ẩn'), '.ẩn');
      expect(formatBytes(512 * 1024), '512 KB');
      expect(formatBytes(4831838), '4,6 MB');
      expect(formatTakeDate(DateTime(2026, 11, 2, 9, 5)), '02/11 09:05');
      expect(nudgeLabel(0), 'Như lúc thu');
      expect(nudgeLabel(10), 'Giọng sớm hơn 10 ms');
      expect(nudgeLabel(-5), 'Giọng muộn hơn 5 ms');
      expect(
        Genre.values.map(genreColor).toSet(),
        hasLength(Genre.values.length),
      );
    });
  });

  group('phiên thu → bản thu', () {
    test('ghép số đo engine + thông số lúc hát + độ trễ theo đường ra', () {
      const status = RecordStatus(
        state: RecordState.done,
        result: 0,
        sampleRate: 48000,
        frames: 480000,
        droppedFrames: 0,
        startBeatFrame: 480000,
        startHostTimeNs: 123,
        inputLatencyMs: 2,
        outputLatencyMs: 4,
        pitchLatencyFrames: 480,
        outputLookaheadFrames: 48,
        route: AudioRoute.speaker,
      );
      const snap = TakeSnapshot(
        genre: Genre.pop,
        correction: CorrectionPreset.singer,
        retuneMs: 15,
        key: MusicKey(11, MusicScale.naturalMinor),
        songTimeline: [
          KeySegment(
            start: Duration.zero,
            end: Duration(seconds: 30),
            key: _am,
          ),
        ],
        tuningCents: 0,
        tempoBpm: 120,
        fxMix: 1,
        gateOn: true,
        transpose: 2,
        voiceGain: 1.2,
        beatGain: 0.8,
      );
      final info = takeInfoFor(
        song: const SongRef(path: '/x/a.m4a', name: 'a.m4a', size: 9),
        status: status,
        snap: snap,
        latencyHistory: const [
          LatencyEntry(
            route: AudioRoute.speaker,
            measuredMs: 12.6,
            reportedMs: 8,
          ),
        ],
        now: DateTime(2026, 11, 2),
      );
      expect(info.roundTripMs, 12.6);
      expect(info.latencySource, LatencySource.measured);
      expect(info.duration, const Duration(seconds: 10));
      expect(info.timeline.single.end, const Duration(seconds: 20));
      expect(
        info.timeline.single.key,
        const MusicKey(11, MusicScale.naturalMinor),
      );
      expect(info.startHostTimeNs, 123);
      final rs = renderSettingsFor(info, preset: const FxPreset());
      expect(rs.voiceOffsetFrames, 605 + 48);
      expect(rs.correction, CorrectionPreset.singer);
      expect(rs.retuneMs, 15);
      expect(rs.gate, isTrue);
      expect(rs.voiceGain, 1.2);
      expect(rs.format, RenderFormat.m4a);
      expect(rs.keyTimeline, hasLength(1));
    });
  });

  group('video camera', () {
    test('độ lệch hình ↔ tiếng theo đồng hồ máy, trừ phần cắt đầu', () {
      const status = RecordStatus(
        state: RecordState.done,
        result: 0,
        sampleRate: 48000,
        frames: 480000,
        droppedFrames: 0,
        startBeatFrame: 0,
        startHostTimeNs: 1000000000000,
        inputLatencyMs: 2,
        outputLatencyMs: 5,
        pitchLatencyFrames: 0,
        outputLookaheadFrames: 48,
        route: AudioRoute.wired,
      );
      const snap = TakeSnapshot(
        genre: Genre.dry,
        correction: null,
        retuneMs: null,
        key: _am,
        songTimeline: [],
        tuningCents: 0,
        tempoBpm: 100,
        fxMix: 1,
        gateOn: false,
        transpose: 0,
        voiceGain: 1,
        beatGain: 1,
      );
      // Khung hình đầu chụp 120 ms sau lúc frame đầu bản thu ra thiết bị.
      const clip = CameraClip(
        path: '/x/20261102-210500/camera.mp4',
        firstFrameNs: 1000120000000,
        duration: Duration(seconds: 10),
        frames: 300,
        dropped: 1,
      );
      final info = takeInfoFor(
        song: const SongRef(path: '/a.m4a', name: 'a.m4a', size: 1),
        status: status,
        snap: snap,
        latencyHistory: const [],
        camera: clip,
      );
      expect(info.videoFile, 'camera.mp4');
      expect(info.videoDroppedFrames, 1);
      // 120 ms − (độ trễ ra 5 ms + limiter 1 ms) = 114 ms.
      expect(cameraOffsetNs(info), 114000000);
      // Cắt đầu 0,5 s: hình bắt đầu sớm hơn 0,5 s trên trục file xuất.
      expect(
        cameraOffsetNs(info.copyWith(trimStartFrame: 24000)),
        114000000 - 500000000,
      );
      final back = TakeInfo.fromJson(
        jsonDecode(jsonEncode(info.toJson())) as Map<String, dynamic>,
      );
      expect(back.videoFile, 'camera.mp4');
      expect(back.videoFirstFrameNs, 1000120000000);
      expect(_info().toJson()['video'], isNull);
      expect(TakeInfo.fromJson(_info().toJson()).videoFile, isNull);
    });
  });

  group('kho bản thu', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('takes'));
    tearDown(() => dir.deleteSync(recursive: true));

    test('xuất bị ngắt (M8 B7): mở lại thì xoá file dở của lần đó, giữ file cũ, trả việc cần xuất lại', () async {
      final take = Take(dir, _info());
      expect(
        await take.recoverInterruptedExport(),
        isNull,
      ); // chưa xuất lần nào
      final old = File('${dir.path}/xuat-090000.m4a')..writeAsStringSync('cũ');
      old.setLastModifiedSync(
        DateTime.now().subtract(const Duration(hours: 1)),
      );
      await take.beginExport(ExportKind.waveformVideo);
      final partAudio = File('${dir.path}/xuat-100000.m4a')
        ..writeAsStringSync('dở');
      final partVideo = File('${dir.path}/video-100001.mp4')
        ..writeAsStringSync('dở');
      File('${dir.path}/dry.wav').writeAsStringSync('giọng');
      // App bị đóng giữa chừng → lần mở sau:
      expect(await take.recoverInterruptedExport(), ExportKind.waveformVideo);
      expect(partAudio.existsSync(), isFalse);
      expect(partVideo.existsSync(), isFalse);
      expect(old.existsSync(), isTrue);
      expect(File('${dir.path}/dry.wav').existsSync(), isTrue);
      expect(await take.recoverInterruptedExport(), isNull); // đã dọn
      // Xuất xong bình thường: không còn dấu.
      await take.beginExport(ExportKind.audio);
      await take.endExport();
      expect(await take.recoverInterruptedExport(), isNull);
    });

    Future<Take> make(TakeStore store, DateTime at) async {
      final d = await store.newTakeDir(at);
      for (final f in ['dry.wav', 'wet.wav', 'beat.wav']) {
        File('${d.path}/$f').writeAsBytesSync(List.filled(100, 0));
      }
      final info = _info(at: at);
      await store.save(d, info);
      return Take(d, info);
    }

    test('lưu, liệt kê mới nhất trước, bỏ bản thu thiếu file, xoá', () async {
      final store = TakeStore(baseDir: dir);
      expect(await store.list(), isEmpty);
      final a = await make(store, DateTime(2026, 11, 1, 10));
      final b = await make(store, DateTime(2026, 11, 2, 10));
      final c = await make(store, DateTime(2026, 11, 2, 10)); // cùng giây
      expect(c.id, '${b.id}-2');
      File(c.dryPath).deleteSync(); // hỏng
      final list = await store.list();
      expect(list.map((t) => t.id), [b.id, a.id]);
      expect(list.first.sizeBytes(), greaterThan(300));
      await store.delete(a);
      expect((await store.list()).map((t) => t.id), [b.id]);
    });

    testWidgets('màn "Bản thu của tôi": trống / có bản thu', (tester) async {
      final store = TakeStore(baseDir: dir);
      await tester.pumpWidget(MaterialApp(home: TakesPage(store: store)));
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 50)),
      );
      await tester.pump();
      expect(find.textContaining('Chưa có bản thu'), findsOneWidget);

      await tester.runAsync(() async {
        await make(store, DateTime(2026, 11, 2, 21, 5));
        await tester.pumpWidget(
          MaterialApp(
            home: TakesPage(key: UniqueKey(), store: store),
          ),
        );
        await Future<void>.delayed(const Duration(milliseconds: 200));
      });
      await tester.pump();
      expect(
        find.text('Lạc trôi'),
        findsOneWidget,
      ); // tên bài không kèm đuôi file
      expect(find.textContaining('02/11 21:05 · 0:30'), findsOneWidget);
    });
  });
}
