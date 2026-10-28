import 'dart:io';

import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/engine/labels.dart';
import 'package:chuantone/engine/playback_watch.dart';
import 'package:chuantone/engine/transpose_timing.dart';
import 'package:chuantone/widgets/transpose_control.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  group('nhãn đổi tone', () {
    test('Gốc / +n / −n (dấu trừ thật)', () {
      expect(transposeLabel(0), 'Gốc');
      expect(transposeLabel(2), '+2');
      expect(transposeLabel(-3), '−3');
    });

    test('tone sau khi đổi và quy về tone gốc', () {
      const am = MusicKey(9, MusicScale.naturalMinor);
      expect(transposedKey(am, 2), const MusicKey(11, MusicScale.naturalMinor));
      expect(transposedKey(am, 5), const MusicKey(2, MusicScale.naturalMinor));
      expect(
        transposedKey(am, -10),
        const MusicKey(11, MusicScale.naturalMinor),
      );
      // Beat đang +2, người dùng chọn Si thứ (đang nghe) → engine nhận La thứ (gốc).
      expect(
        baseKeyForHeard(const MusicKey(11, MusicScale.naturalMinor), 2),
        am,
      );
      expect(
        baseKeyForHeard(const MusicKey(0, MusicScale.major), -3),
        const MusicKey(3, MusicScale.major),
      );
    });
  });

  group('nhớ tone theo bài', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('song_prefs'));
    tearDown(() => dir.deleteSync(recursive: true));

    test('lưu, đọc lại, về 0 thì xoá', () async {
      final prefs = SongPrefs(baseDir: dir);
      expect(await prefs.transposeFor(name: 'a.m4a', size: 10), 0);
      await prefs.setTranspose(name: 'a.m4a', size: 10, semitones: -2);
      await prefs.setTranspose(name: 'b.mp3', size: 20, semitones: 4);
      expect(
        await SongPrefs(baseDir: dir).transposeFor(name: 'a.m4a', size: 10),
        -2,
      );
      expect(await prefs.transposeFor(name: 'b.mp3', size: 20), 4);
      expect(await prefs.transposeFor(name: 'a.m4a', size: 11), 0); // khác file
      await prefs.setTranspose(name: 'a.m4a', size: 10, semitones: 0);
      expect(await prefs.transposeFor(name: 'a.m4a', size: 10), 0);
      expect(
        File('${dir.path}/song_prefs.json').readAsStringSync(),
        isNot(contains('a.m4a')),
      );
    });

    test('file hỏng hoặc giá trị ngoài −6…+6 → coi như 0', () async {
      File('${dir.path}/song_prefs.json')
          .writeAsStringSync('{"a|1": {"transpose": 9}');
      expect(await SongPrefs(baseDir: dir).transposeFor(name: 'a', size: 1), 0);
      File('${dir.path}/song_prefs.json')
          .writeAsStringSync('{"a|1": {"transpose": 9}}');
      expect(await SongPrefs(baseDir: dir).transposeFor(name: 'a', size: 1), 0);
    });

    test('giữ tối đa ${SongPrefs.maxEntries} bài, bỏ bài cũ nhất', () async {
      final prefs = SongPrefs(baseDir: dir);
      for (var i = 0; i <= SongPrefs.maxEntries; i++) {
        await prefs.setTranspose(name: 'bai$i', size: i, semitones: 1);
      }
      expect(await prefs.transposeFor(name: 'bai0', size: 0), 0);
      expect(await prefs.transposeFor(name: 'bai1', size: 1), 1);
    });
  });

  group('nút đổi tone', () {
    Future<List<int>> pump(
      WidgetTester tester, {
      required int value,
      int? audible,
      double progress = 1.0,
    }) async {
      final calls = <int>[];
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: TransposeControl(
              value: value,
              audible: audible ?? value,
              progress: progress,
              onChanged: calls.add,
            ),
          ),
        ),
      );
      return calls;
    }

    testWidgets('bấm + / − đổi một nửa cung', (tester) async {
      final calls = await pump(tester, value: 2);
      expect(find.text('+2'), findsOneWidget);
      await tester.tap(find.byTooltip('Nâng tone'));
      await tester.tap(find.byTooltip('Hạ tone'));
      expect(calls, [3, 1]);
      expect(find.textContaining('Đã nâng 2 nửa cung'), findsOneWidget);
    });

    testWidgets('chặn ở ±6', (tester) async {
      final calls = await pump(tester, value: 6);
      await tester.tap(find.byTooltip('Nâng tone'));
      expect(calls, isEmpty);
      final down = await pump(tester, value: -6);
      await tester.tap(find.byTooltip('Hạ tone'));
      expect(down, isEmpty);
    });

    testWidgets('đang chờ bản mới / đang render cả bài', (tester) async {
      await pump(tester, value: 3, audible: 0, progress: 0.1);
      expect(find.text('Đang đổi tone…'), findsOneWidget);
      await pump(tester, value: 3, progress: 0.4);
      expect(find.text('Đang chuẩn bị cả bài: 40%'), findsOneWidget);
      await pump(tester, value: 0);
      expect(find.byType(LinearProgressIndicator), findsNothing);
      expect(find.text('Gốc'), findsOneWidget);
    });
  });

  group('số đo đổi tone (Phòng thử)', () {
    const ms = Duration(milliseconds: 1);

    test('bấm → nghe: tính từ lần bấm cuối tới khi nghe đúng mức đó', () {
      final t = TransposeTiming();
      expect(transposeTimingLabel(t), 'chưa đổi');
      t.requested(1, ms * 1000);
      t.requested(2, ms * 1100); // bấm tiếp trước khi kịp nghe +1
      expect(t.observe(1, ms * 1120), isFalse);
      expect(transposeTimingLabel(t), 'đang đổi…');
      expect(t.observe(2, ms * 1140), isTrue);
      expect(t.lastSwitch, ms * 40);
      expect(t.observe(2, ms * 1200), isFalse); // đã đo xong
      expect(transposeTimingLabel(t), 'nghe sau 40 ms (+2)');
    });

    test('render cả bài: chỉ nhận của mức đang chọn', () {
      final t = TransposeTiming()..requested(-3, Duration.zero);
      t.observe(-3, ms * 30);
      t.rendered(2, 2500); // bản cũ, đã bỏ
      expect(t.lastRender, isNull);
      t.rendered(-3, 3140.4);
      expect(transposeTimingLabel(t), 'nghe sau 30 ms (−3) · cả bài 3.1 s');
      t.reset();
      expect(transposeTimingLabel(t), 'chưa đổi');
    });
  });

  group('chẩn đoán vị trí sau khi đổi tone', () {
    const ms = Duration(milliseconds: 1);

    test('phát đều: ×1.00, không nhảy lùi, không khựng', () {
      final w = PlaybackWatch()..restart(Duration.zero);
      for (var i = 0; i <= 300; i++) {
        w.observe(
          at: ms * (33 * i),
          position: ms * (60000 + 33 * i),
          playing: true,
        );
      }
      expect(w.rate, closeTo(1.0, 1e-9));
      expect(w.jumpsBack, 0);
      expect(w.stalls, 0);
      expect(
        playbackWatchLabel(w, ms * 9900),
        'vị trí ×1.00 · nhảy lùi 0 · khựng 0 (đang đo)',
      );
      expect(
        playbackWatchLabel(w, ms * 16000),
        'vị trí ×1.00 · nhảy lùi 0 · khựng 0',
      );
    });

    test('lặp đoạn và đứng chờ đều bị đếm; tạm dừng thì không tính', () {
      final w = PlaybackWatch()..restart(Duration.zero);
      var pos = 60000;
      for (var i = 0; i <= 200; i++) {
        if (i == 60) pos -= 3000; // nhảy lùi 3 s
        if (i < 100 || i >= 110) pos += 33; // 10 nhịp (330 ms) đứng yên
        w.observe(
          at: ms * (33 * i),
          position: ms * pos,
          playing: i < 150 || i > 170,
        );
      }
      expect(w.jumpsBack, 1);
      expect(w.maxJumpBack, ms * 2967);
      expect(w.stalls, 1);
      expect(w.stalled, ms * 330);
      expect(
        playbackWatchLabel(w, ms * 20000),
        'vị trí ×0.94 · nhảy lùi 1 (xa nhất 3.0 s) · khựng 1 (330 ms)',
      );
    });
  });
}
