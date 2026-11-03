import 'dart:io';

import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/lrc.dart';
import 'package:chuantone/data/sample_songs.dart';
import 'package:chuantone/data/song_import.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/pages/songs_page.dart';
import 'package:chuantone/widgets/lyrics_view.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

const _lrc = '''
[ti:Bài thử]
[00:01.00]Câu một
[00:03.00]<00:03.00>Đô <00:04.00>Rê<00:05.00>
[00:06.00]Câu ba
''';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory dir;
  late Directory outside;
  setUp(() {
    dir = Directory.systemTemp.createTempSync('lyrics');
    outside = Directory.systemTemp.createTempSync('lyrics_src');
  });
  tearDown(() {
    dir.deleteSync(recursive: true);
    outside.deleteSync(recursive: true);
  });
  tearDownAll(LibraryDb.closeAll);

  File file(String name, String text) =>
      File('${outside.path}/$name')..writeAsStringSync(text);
  File song(String name, int bytes) =>
      File('${outside.path}/$name')..writeAsBytesSync(List.filled(bytes, 3));

  group('lời trong thư viện', () {
    test('gắn, đọc lại, lệch lời nhớ theo bài, bỏ lời', () async {
      final lib = SongLibrary(baseDir: dir);
      final s = await lib.add(song('Bài A.mp3', 10).path);
      expect(s.hasLyrics, isFalse);
      expect(await lib.lyricsFor(s.name, s.size), isNull);

      final ly = await lib.setLyrics(s.name, s.size, file('a.lrc', _lrc).path);
      expect(ly.lines, hasLength(3));
      final again = (await lib.find(s.name, s.size))!;
      expect(again.hasLyrics, isTrue);
      expect(songSubtitle(again), 'Chưa dò tone · có lời');

      await lib.setLyricsOffset(s.name, s.size, -1500);
      final r = (await lib.lyricsFor(s.name, s.size))!;
      expect(r.lyrics.title, 'Bài thử');
      expect(r.offsetMs, -1500);
      expect((await lib.find(s.name, s.size))!.lyricsOffsetMs, -1500);

      // Gắn lời mới: lệch lời về 0.
      await lib.setLyrics(s.name, s.size, file('b.lrc', _lrc).path);
      expect((await lib.lyricsFor(s.name, s.size))!.offsetMs, 0);

      await lib.removeLyrics(s.name, s.size);
      expect(await lib.lyricsFor(s.name, s.size), isNull);
      expect((await lib.find(s.name, s.size))!.hasLyrics, isFalse);
    });

    test(
      'file không phải LRC: báo lỗi, giữ lời cũ; bài chưa có: lỗi',
      () async {
        final lib = SongLibrary(baseDir: dir);
        final s = await lib.add(song('Bài B.mp3', 11).path);
        await lib.setLyrics(s.name, s.size, file('ok.lrc', _lrc).path);
        await expectLater(
          lib.setLyrics(s.name, s.size, file('x.txt', 'không có mốc').path),
          throwsFormatException,
        );
        expect(
          (await lib.lyricsFor(s.name, s.size))!.lyrics.lines,
          hasLength(3),
        );
        await expectLater(
          lib.setLyrics('khong-co.mp3', 1, file('c.lrc', _lrc).path),
          throwsStateError,
        );
      },
    );

    test(
      'xoá bài xoá cả file lời; file lời hỏng trên đĩa → không có lời',
      () async {
        final lib = SongLibrary(baseDir: dir);
        final s = await lib.add(song('Bài C.mp3', 12).path);
        await lib.setLyrics(s.name, s.size, file('c.lrc', _lrc).path);
        final lrcFiles = dir
            .listSync(recursive: true)
            .whereType<File>()
            .where((f) => f.path.endsWith('.lrc'));
        expect(lrcFiles, hasLength(1));
        lrcFiles.single.writeAsStringSync('hỏng');
        expect(await lib.lyricsFor(s.name, s.size), isNull);
        await lib.delete((await lib.find(s.name, s.size))!);
        expect(
          dir.listSync(recursive: true).where((f) => f.path.endsWith('.lrc')),
          isEmpty,
        );
      },
    );
  });

  group('ghép file lời với file nhạc', () {
    test('cùng tên (bỏ dấu, hoa thường); một nhạc + một lời thì ghép luôn', () {
      expect(
        pairLyrics(
          ['Duyên Phận.mp3', 'Lạc Trôi.m4a'],
          ['lac troi.lrc', 'DUYÊN PHẬN.lrc'],
        ),
        {'Duyên Phận.mp3': 'DUYÊN PHẬN.lrc', 'Lạc Trôi.m4a': 'lac troi.lrc'},
      );
      expect(pairLyrics(['beat.mp3'], ['loi bai hat.lrc']), {
        'beat.mp3': 'loi bai hat.lrc',
      });
      expect(pairLyrics(['a.mp3', 'b.mp3'], ['c.lrc']), isEmpty);
      expect(pairLyrics(['a.mp3'], []), isEmpty);
      expect(isLyricsFile('Bài.LRC'), isTrue);
      expect(isLyricsFile('loi.txt'), isTrue);
      expect(isAudioFile('Bài.M4A'), isTrue);
      expect(sameBaseName('Đắp Mộ.mp3', 'dap mo.lrc'), isTrue);
    });
  });

  group('bài mẫu (asset thật)', () {
    test('lời của mọi bài mẫu đọc được, có mốc từng chữ', () async {
      for (final s in sampleSongs) {
        final ly = parseLrc(await rootBundle.loadString(s.lyricsAsset));
        expect(ly.title, s.title);
        expect(ly.lines.length, greaterThanOrEqualTo(5), reason: s.id);
        expect(ly.lines.any((l) => l.words.length >= 4), isTrue, reason: s.id);
        final audio = await rootBundle.load(s.audioAsset);
        expect(audio.lengthInBytes, inInclusiveRange(200000, 1500000));
      }
    });

    test(
      'chép vào thư viện kèm lời, đánh dấu bài mẫu; lần sau dùng lại',
      () async {
        final lib = SongLibrary(baseDir: dir);
        final s = sampleSongs.first;
        final a = await installSample(lib, s);
        expect(a.title, 'Luyện giọng: Đô Rê Mi');
        expect(a.sample, isTrue);
        expect(a.hasLyrics, isTrue);
        expect(a.missing, isFalse);
        expect(File(a.path).existsSync(), isTrue);
        final b = await installSample(lib, s);
        expect(b.path, a.path);
        expect(await lib.songs(), hasLength(1));
        // Lời bị bỏ: bấm lại thì nạp lại lời.
        await lib.removeLyrics(a.name, a.size);
        expect((await installSample(lib, s)).hasLyrics, isTrue);
      },
    );
  });

  group('lời chạy chữ', () {
    testWidgets('dòng đang hát tô màu dần, dòng sau mờ, lệch lời', (
      tester,
    ) async {
      final lyrics = parseLrc(_lrc);
      final pos = ValueNotifier(Duration.zero);
      addTearDown(pos.dispose);
      var offset = 0;
      Widget view() => MaterialApp(
        home: Scaffold(
          body: LyricsView(lyrics: lyrics, offsetMs: offset, position: pos),
        ),
      );
      await tester.pumpWidget(view());
      String current() => tester
          .widget<Text>(find.byKey(const ValueKey('lyrics-current')))
          .textSpan!
          .toPlainText();
      String sung() {
        final span =
            tester
                    .widget<Text>(find.byKey(const ValueKey('lyrics-current')))
                    .textSpan!
                as TextSpan;
        return (span.children!.first as TextSpan).text!;
      }

      String next() =>
          tester.widget<Text>(find.byKey(const ValueKey('lyrics-next'))).data!;

      expect(current(), '');
      expect(next(), '♪ Câu một');

      pos.value = const Duration(milliseconds: 3500);
      await tester.pump();
      expect(current(), 'Đô Rê');
      expect(sung(), 'Đô'); // giữa "Đô " → 2 ký tự
      expect(next(), 'Câu ba');

      pos.value = const Duration(seconds: 5);
      await tester.pump();
      expect(sung(), 'Đô Rê');

      // Lời sớm hơn 2 s: ở 4 s đã là "Câu ba".
      offset = 2000;
      pos.value = const Duration(seconds: 4);
      await tester.pumpWidget(view());
      expect(current(), 'Câu ba');
      expect(next(), '');
    });

    testWidgets('màn hẹp chữ 130%: chiều cao cố định, không tràn', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(640, 1136);
      tester.view.devicePixelRatio = 2;
      tester.platformDispatcher.textScaleFactorTestValue = 1.3;
      addTearDown(tester.view.reset);
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      final lyrics = parseLrc(
        '[00:00.00]Một câu hát rất dài để xem có bị tràn ra ngoài màn hình nhỏ hay không '
        'khi chữ phóng to một trăm ba mươi phần trăm\n[00:05.00]Câu sau cũng dài dài dài dài dài\n',
      );
      final pos = ValueNotifier(const Duration(seconds: 1));
      addTearDown(pos.dispose);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Column(
              children: [
                LyricsView(lyrics: lyrics, offsetMs: 0, position: pos),
              ],
            ),
          ),
        ),
      );
      final h1 = tester.getSize(find.byType(LyricsView)).height;
      pos.value = const Duration(seconds: 6);
      await tester.pump();
      expect(tester.getSize(find.byType(LyricsView)).height, h1);
      expect(tester.takeException(), isNull);
    });
  });
}
