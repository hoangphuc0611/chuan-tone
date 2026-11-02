import 'dart:convert';
import 'dart:io';

import 'package:chuantone/app.dart';
import 'package:chuantone/data/analysis_cache.dart';
import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/data/stem_store.dart';
import 'package:chuantone/engine_info.dart';
import 'package:chuantone/pages/songs_page.dart';
import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

const _gm = MusicKey(7, MusicScale.naturalMinor);

SongAnalysis _analysis({MusicKey? key = _gm, double? bpm = 85}) => SongAnalysis(
  duration: const Duration(minutes: 5),
  sampleRate: 44100,
  channels: 2,
  key: key,
  keyConfidence: 0.8,
  tuningCents: 0,
  bpm: bpm,
  bpmConfidence: 0.7,
  integratedLufs: -12,
  samplePeakDb: -0.5,
  timeline: const [],
);

void main() {
  late Directory dir;
  late Directory outside; // file "của người dùng" ngoài thư mục app
  setUp(() {
    dir = Directory.systemTemp.createTempSync('library');
    outside = Directory.systemTemp.createTempSync('library_src');
  });
  tearDown(() {
    dir.deleteSync(recursive: true);
    outside.deleteSync(recursive: true);
  });

  File song(String name, int bytes) =>
      File('${outside.path}/$name')..writeAsBytesSync(List.filled(bytes, 7));

  group('khoá bài', () {
    test('tách tên + kích thước; tên có dấu |', () {
      expect(songKey('a|b.mp3', 12), 'a|b.mp3|12');
      expect(splitSongKey('a|b.mp3|12'), (name: 'a|b.mp3', size: 12));
      expect(splitSongKey('khongco'), isNull);
      expect(splitSongKey('a|x'), isNull);
    });

    test('tên hiển thị từ tên file', () {
      expect(titleFromFileName('Duyen_Phan - Beat.mp3'), 'Duyen Phan - Beat');
      expect(titleFromFileName('.mp3'), '.mp3');
      expect(titleFromFileName('khong duoi'), 'khong duoi');
    });

    test('bỏ dấu tiếng Việt, cả chữ tổ hợp', () {
      expect(foldVietnamese('Duyên Phận ĐỜI'), 'duyen phan doi');
      // "Duyên" dạng e + dấu mũ rời (U+0302) như tên file trên iOS / macOS.
      expect(foldVietnamese('Duyên'), 'duyen');
    });
  });

  group('chuyển dữ liệu JSON cũ (M5–M7) vào SQLite', () {
    test('lựa chọn theo bài, preset gần nhất, phân tích, cài đặt', () async {
      File('${dir.path}/song_prefs.json').writeAsStringSync(
        '{"cu.mp3|10": {"transpose": -2, "genre": "bolero"},'
        ' "moi.m4a|20": {"key": {"root": 7, "scale": "naturalMinor"}},'
        ' "hong|x": {"transpose": 1}, "kieusai.mp3|5": {"transpose": "2"},'
        ' "*lastGenre": "ballad"}',
      );
      File('${dir.path}/analysis_cache.json').writeAsStringSync(
        '{"0.8.0|moi.m4a|20": ${_json(_analysis())}, "hong": {}}',
      );
      File('${dir.path}/settings.json').writeAsStringSync(
        '{"separationServer": "http://1.2.3.4:8000", "separationConsent": true,'
        ' "deviceId": "0123456789abcdef0123456789abcdef"}',
      );

      final prefs = SongPrefs(baseDir: dir);
      expect(await prefs.transposeFor(name: 'cu.mp3', size: 10), -2);
      expect(await prefs.genreFor(name: 'cu.mp3', size: 10), Genre.bolero);
      expect(await prefs.keyFor(name: 'moi.m4a', size: 20), _gm);
      expect(await prefs.transposeFor(name: 'kieusai.mp3', size: 5), 0);
      expect(await prefs.lastGenre(), Genre.ballad);

      final got = await AnalysisCache(
        engineVersion: '0.8.0',
        baseDir: dir,
      ).lookup(name: 'moi.m4a', size: 20);
      expect(got?.key, _gm);
      expect(got?.bpm, closeTo(85, 1e-9));

      final s = AppSettings(baseDir: dir);
      expect(await s.separationServer(), 'http://1.2.3.4:8000');
      expect(await s.separationConsent(), isTrue);
      expect(await s.deviceId(), '0123456789abcdef0123456789abcdef');

      for (final f in ['song_prefs', 'analysis_cache', 'settings']) {
        expect(File('${dir.path}/$f.json').existsSync(), isFalse);
        expect(File('${dir.path}/$f.json.migrated').existsSync(), isTrue);
      }
      // Chưa có file trong thư viện → chưa hiện trong "Bài hát của tôi".
      expect(await SongLibrary(baseDir: dir).songs(), isEmpty);
    });

    test('thứ tự cũ → mới được giữ (bỏ bài cũ nhất khi đầy)', () async {
      final entries = [
        for (var i = 0; i < SongPrefs.maxEntries; i++)
          '"b$i|$i": {"transpose": 1}',
      ];
      File('${dir.path}/song_prefs.json')
          .writeAsStringSync('{${entries.join(',')}}');
      final prefs = SongPrefs(baseDir: dir);
      await prefs.setTranspose(name: 'moi', size: 1, semitones: 3);
      expect(await prefs.transposeFor(name: 'b0', size: 0), 0); // cũ nhất bị bỏ
      expect(await prefs.transposeFor(name: 'b1', size: 1), 1);
      expect(await prefs.transposeFor(name: 'moi', size: 1), 3);
    });

    test('chỉ chuyển một lần: file JSON ghi sau đó không được đọc', () async {
      expect(await SongPrefs(baseDir: dir).lastGenre(), isNull);
      File('${dir.path}/song_prefs.json')
          .writeAsStringSync('{"*lastGenre": "rap"}');
      expect(await SongPrefs(baseDir: dir).lastGenre(), isNull);
    });
  });

  group('thư viện bài', () {
    test('thêm (chép), thêm lại không nhân đôi, chuyển file tạm', () async {
      final lib = SongLibrary(baseDir: dir);
      final src = song('Duyên Phận.MP3', 1000);
      final a = await lib.add(src.path);
      expect(a.title, 'Duyên Phận');
      expect(a.size, 1000);
      expect(a.path, startsWith('${dir.path}/library/'));
      expect(a.path, endsWith('.mp3'));
      expect(File(a.path).lengthSync(), 1000);
      expect(src.existsSync(), isTrue); // chép, không chuyển

      // Bộ chọn file trên iPhone đưa bản chép khác chỗ, cùng tên + cỡ → không chép lại, xoá bản tạm.
      final tmp = File('${outside.path}/tmp_copy')
        ..writeAsBytesSync(List.filled(1000, 7));
      final again = await lib.add(tmp.path, name: 'Duyên Phận.MP3', move: true);
      expect(again.path, a.path);
      expect(tmp.existsSync(), isFalse);
      expect(await lib.songs(), hasLength(1));

      final moved = song('lac troi.m4a', 500);
      final b = await lib.add(moved.path, move: true);
      expect(moved.existsSync(), isFalse);
      expect(File(b.path).lengthSync(), 500);
      expect((await lib.songs()).map((s) => s.title), [
        'lac troi',
        'Duyên Phận',
      ]);
    });

    test('giữ lựa chọn đã có từ trước khi bài vào thư viện', () async {
      await SongPrefs(baseDir: dir)
          .setTranspose(name: 'cu.mp3', size: 300, semitones: -3);
      final lib = SongLibrary(baseDir: dir);
      final s = await lib.add(song('cu.mp3', 300).path);
      expect(s.transpose, -3);
      expect(
        await SongPrefs(baseDir: dir).transposeFor(name: 'cu.mp3', size: 300),
        -3,
      );
    });

    test('mới dùng lên đầu, tìm không dấu, đổi tên, kèm kết quả dò', () async {
      final lib = SongLibrary(baseDir: dir);
      final a = await lib.add(song('Duyên Phận.mp3', 10).path);
      final b = await lib.add(song('Lạc Trôi.mp3', 20).path);
      expect((await lib.songs()).first.key, b.key);
      await lib.touch(a.name, a.size);
      final list = await lib.songs();
      expect(list.first.key, a.key);
      expect(list.first.singCount, 1);

      expect((await lib.songs(query: 'duyen')).map((s) => s.key), [a.key]);
      expect((await lib.songs(query: 'TRÔI')).map((s) => s.key), [b.key]);
      expect(await lib.songs(query: 'xyz'), isEmpty);
      expect(await lib.songs(limit: 1), hasLength(1));

      await lib.rename(a, '  Duyên phận (tone nữ) ');
      await lib.rename(a, '   '); // rỗng → giữ tên cũ
      expect((await lib.find(a.name, a.size))!.title, 'Duyên phận (tone nữ)');

      await AnalysisCache(
        engineVersion: '0.9.0',
        baseDir: dir,
      ).store(name: a.name, size: a.size, analysis: _analysis());
      final withKey = await lib.find(a.name, a.size, engineVersion: '0.9.0');
      expect(withKey!.key0, _gm);
      expect(songSubtitle(withKey), 'Son thứ (Gm) · 85 BPM');
      expect(
        (await lib.find(a.name, a.size, engineVersion: '0.8.0'))!.analysis,
        isNull,
      );

      await SongPrefs(baseDir: dir)
          .setTranspose(name: a.name, size: a.size, semitones: 2);
      await SongPrefs(baseDir: dir).setKey(
        name: a.name,
        size: a.size,
        key: const MusicKey(2, MusicScale.major),
      );
      expect(
        songSubtitle((await lib.find(a.name, a.size, engineVersion: '0.9.0'))!),
        'Rê trưởng (D) · 85 BPM · tone +2',
      );
    });

    test('xoá bài: file, giọng đã tách, kết quả dò, lựa chọn', () async {
      final lib = SongLibrary(baseDir: dir);
      final a = await lib.add(song('a.mp3', 10).path);
      final keep = await lib.add(song('b.mp3', 10).path);
      final stems = StemStore(baseDir: dir);
      final sd = await stems.prepare(name: a.name, size: a.size);
      await AnalysisCache(
        engineVersion: '0.9.0',
        baseDir: dir,
      ).store(name: a.name, size: a.size, analysis: _analysis());
      await SongPrefs(baseDir: dir)
          .setGenre(name: a.name, size: a.size, genre: Genre.rap);

      await lib.delete(a);
      expect(File(a.path).existsSync(), isFalse);
      expect(sd.existsSync(), isFalse);
      expect(
        await AnalysisCache(
          engineVersion: '0.9.0',
          baseDir: dir,
        ).lookup(name: a.name, size: a.size),
        isNull,
      );
      expect(
        await SongPrefs(baseDir: dir).genreFor(name: a.name, size: a.size),
        isNull,
      );
      expect((await lib.songs()).map((s) => s.key), [keep.key]);
      expect(await lib.totalBytes(), 10);
    });

    test('file nhạc bị mất → đánh dấu', () async {
      final lib = SongLibrary(baseDir: dir);
      final a = await lib.add(song('a.mp3', 10).path);
      File(a.path).deleteSync();
      final s = (await lib.songs()).single;
      expect(s.missing, isTrue);
      expect(songSubtitle(s), 'File nhạc không còn trên máy');
      // Thêm lại đúng file → có file trở lại.
      final again = await lib.add(song('a.mp3', 10).path);
      expect(again.missing, isFalse);
    });
  });

  group('màn "Bài hát của tôi"', () {
    Future<void> pump(
      WidgetTester tester,
      SongLibrary lib, {
      List<LibrarySong>? opened,
    }) async {
      tester.view.physicalSize = const Size(320 * 3, 640 * 3);
      tester.view.devicePixelRatio = 3;
      addTearDown(tester.view.reset);
      await tester.runAsync(() async {
        await tester.pumpWidget(
          MediaQuery(
            data: const MediaQueryData(textScaler: TextScaler.linear(1.3)),
            child: MaterialApp(
              home: SongsPage(
                key: UniqueKey(),
                library: lib,
                engineVersion: '0.9.0',
                onOpen: (_, s) async => opened?.add(s),
              ),
            ),
          ),
        );
        await Future<void>.delayed(const Duration(milliseconds: 100));
      });
      await tester.pump();
    }

    // Hộp thoại đóng → lệnh CSDL + đọc file thật chạy ngoài đồng hồ giả của testWidgets.
    // Mỗi bước đọc file thật cần một vòng runAsync + pump.
    Future<void> settle(WidgetTester tester) async {
      for (var i = 0; i < 10; i++) {
        await tester.pumpAndSettle();
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 10)),
        );
      }
      await tester.pumpAndSettle();
    }

    testWidgets('trống: hướng dẫn thêm bài', (tester) async {
      await pump(tester, SongLibrary(baseDir: dir));
      expect(find.text('Chưa có bài nào'), findsOneWidget);
      expect(find.text('Thêm bài hát'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

    testWidgets(
      'danh sách, tìm không dấu, mở, đổi tên, xoá (màn 320 px, chữ 130%)',
      (tester) async {
        final lib = SongLibrary(baseDir: dir);
        final opened = <LibrarySong>[];
        await tester.runAsync(() async {
          await lib.add(
            song(
              'Lạc Trôi - Beat Tone Nam Rất Dài Để Thử Xuống Dòng.mp3',
              20,
            ).path,
          );
          final a = await lib.add(song('Duyên Phận.mp3', 10).path);
          await AnalysisCache(
            engineVersion: '0.9.0',
            baseDir: dir,
          ).store(name: a.name, size: a.size, analysis: _analysis());
        });
        await pump(tester, lib, opened: opened);
        expect(tester.takeException(), isNull);
        expect(find.text('Duyên Phận'), findsOneWidget);
        expect(find.text('Son thứ (Gm) · 85 BPM'), findsOneWidget);
        expect(find.text('Chưa dò tone'), findsOneWidget);

        await tester.enterText(find.byType(TextField), 'duyen');
        await tester.pump();
        expect(find.text('Duyên Phận'), findsOneWidget);
        expect(find.textContaining('Lạc Trôi'), findsNothing);

        await tester.tap(find.text('Duyên Phận'));
        await settle(tester);
        expect(opened.single.title, 'Duyên Phận');

        await tester.tap(find.byTooltip('Tuỳ chọn'));
        await tester.pumpAndSettle();
        await tester.tap(find.text('Đổi tên'));
        await tester.pumpAndSettle();
        await tester.enterText(find.byType(TextField).last, 'Duyên phận (nữ)');
        await tester.tap(find.text('Lưu'));
        await settle(tester);
        // Tên mới vẫn khớp "duyen" (bỏ dấu, không phân biệt hoa thường).
        expect(find.text('Duyên phận (nữ)'), findsOneWidget);

        await tester.tap(find.byTooltip('Tuỳ chọn'));
        await tester.pumpAndSettle();
        await tester.tap(find.text('Xoá bài'));
        await tester.pumpAndSettle();
        expect(
          find.textContaining('Bản thu của bạn vẫn được giữ'),
          findsOneWidget,
        );
        await tester.tap(find.text('Xoá'));
        await settle(tester);
        expect(find.text('Không có bài nào khớp.'), findsOneWidget);
        await tester.tap(find.byTooltip('Xoá chữ'));
        await tester.pump();
        expect(find.textContaining('Lạc Trôi'), findsOneWidget);
        expect(tester.takeException(), isNull);
      },
    );
  });

  group('"Mở bằng ChuẩnTone"', () {
    const channel = MethodChannel('chuantone/incoming');
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    tearDown(() => messenger.setMockMethodCallHandler(channel, null));

    test('lấy hàng chờ; chưa có phần native → rỗng', () async {
      expect(await IncomingFiles.take(), isEmpty);
      messenger.setMockMethodCallHandler(channel, (call) async {
        expect(call.method, 'take');
        return [
          {'path': '/tmp/a/bai.mp3', 'name': 'bài.mp3'},
          {'name': 'thiếu đường dẫn'},
        ];
      });
      final files = await IncomingFiles.take();
      expect(files.single.path, '/tmp/a/bai.mp3');
      expect(files.single.name, 'bài.mp3');
    });

    testWidgets('file mở app lên được chuyển vào thư viện', (tester) async {
      final src = song('tam', 42);
      var served = false;
      messenger.setMockMethodCallHandler(channel, (call) async {
        if (served) return const [];
        served = true;
        return [
          {'path': src.path, 'name': 'Bài từ Zalo.m4a'},
        ];
      });
      final lib = SongLibrary(baseDir: dir);
      await tester.runAsync(() async {
        await tester.pumpWidget(
          ChuanToneApp(
            engineInfo: const EngineInfo(ok: false, error: 'không có engine'),
            library: lib,
          ),
        );
        await Future<void>.delayed(const Duration(milliseconds: 200));
      });
      await tester.pump();
      List<LibrarySong> songs = const [];
      await tester.runAsync(() async => songs = await lib.songs());
      expect(songs.single.title, 'Bài từ Zalo');
      expect(songs.single.size, 42);
      expect(src.existsSync(), isFalse); // đã chuyển, không để lại file tạm
    });
  });

  tearDownAll(LibraryDb.closeAll);
}

String _json(SongAnalysis a) => jsonEncode(analysisToJson(a));
