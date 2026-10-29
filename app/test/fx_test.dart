import 'dart:io';

import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/engine/genre_rules.dart';
import 'package:chuantone/engine/labels.dart';
import 'package:chuantone/widgets/fx_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

void main() {
  group('gợi ý preset theo tên bài', () {
    test('từ khoá thể loại, không phân biệt hoa thường / dấu', () {
      expect(
        genreFromName('Đắp Mộ Cuộc Tình (Bolero) - Beat.mp3'),
        Genre.bolero,
      );
      expect(genreFromName('Nhạc Trữ Tình Tone Nữ.m4a'), Genre.bolero);
      expect(genreFromName('LK Nhạc Vàng.mp3'), Genre.bolero);
      expect(genreFromName('Hãy Trao Cho Anh REMIX.m4a'), Genre.remix);
      expect(genreFromName('Bước qua nhau_Vinahouse.mp3'), Genre.remix);
      expect(genreFromName('Rap Việt - Beat.wav'), Genre.rap);
      expect(genreFromName('beat-hip-hop.mp3'), Genre.rap);
      expect(genreFromName('Có Hẹn Với Thanh Xuân (Lo-fi).mp3'), Genre.lofi);
      expect(genreFromName('Ballad buồn.flac'), Genre.ballad);
    });

    test('tên file dạng dấu rời (NFD, iOS / macOS)', () {
      // "Nhạc Trữ Tình" với dấu kết hợp.
      expect(genreFromName('Nhạc Trữ Tình.mp3'), Genre.bolero);
    });

    test('chỉ khớp nguyên từ; không có từ khoá → null', () {
      expect(genreFromName('Trap Beat.mp3'), isNull);
      expect(genreFromName('grape.mp3'), isNull);
      expect(genreFromName('Bolerox.mp3'), isNull);
      expect(genreFromName('Nơi này có anh - Beat.mp3'), isNull);
      expect(genreFromName('bai.remix'), isNull); // đuôi file không tính
    });

    test('nhiều từ khoá: kiểu phối thắng thể loại bài', () {
      expect(genreFromName('Duyên Phận (Bolero Remix).mp3'), Genre.remix);
      expect(genreFromName('Lofi Remix.mp3'), Genre.lofi);
    });
  });

  group('gợi ý preset theo nhịp', () {
    test('≥ 118 → Nhạc trẻ, ≤ 80 → Ballad, ở giữa / không rõ → null', () {
      expect(genreFromBpm(118), Genre.pop);
      expect(genreFromBpm(140), Genre.pop);
      expect(genreFromBpm(117.9), isNull);
      expect(genreFromBpm(100), isNull);
      expect(genreFromBpm(80.1), isNull);
      expect(genreFromBpm(80), Genre.ballad);
      expect(genreFromBpm(62), Genre.ballad);
      expect(genreFromBpm(null), isNull);
      expect(genreFromBpm(0), isNull);
      expect(genreFromBpm(double.nan), isNull);
    });

    test('thứ tự: đã chọn cho bài → tên → nhịp → không đoán', () {
      expect(suggestGenre(saved: Genre.rap, fileName: 'Bolero.mp3', bpm: 130), (
        genre: Genre.rap,
        source: GenreSource.saved,
      ));
      expect(suggestGenre(fileName: 'Bolero.mp3', bpm: 130), (
        genre: Genre.bolero,
        source: GenreSource.name,
      ));
      expect(suggestGenre(fileName: 'bai-hat.mp3', bpm: 130), (
        genre: Genre.pop,
        source: GenreSource.bpm,
      ));
      expect(suggestGenre(fileName: 'bai-hat.mp3', bpm: 100), isNull);
      expect(suggestGenre(fileName: 'bai-hat.mp3'), isNull);
    });
  });

  test('mức chỉnh giọng theo thể loại (bảng mục 6)', () {
    final dry = genreCorrection(Genre.dry);
    expect(dry.keep, isTrue);
    final rap = genreCorrection(Genre.rap);
    expect(rap.keep, isFalse);
    expect(rap.level, isNull);
    final bolero = genreCorrection(Genre.bolero);
    expect(bolero.level, CorrectionPreset.natural);
    expect(bolero.retuneMs, 45);
    expect(genreCorrection(Genre.ballad).retuneMs, 35);
    expect(genreCorrection(Genre.pop).level, CorrectionPreset.singer);
    expect(genreCorrection(Genre.remix).retuneMs, 8);
    expect(genreCorrection(Genre.lofi).level, CorrectionPreset.natural);
  });

  group('nhãn hiệu ứng', () {
    test('tên preset và vì sao được chọn', () {
      expect(Genre.values.map(genreLabel), [
        'Khô',
        'Bolero',
        'Ballad',
        'Nhạc trẻ',
        'Remix',
        'Rap',
        'Lofi',
      ]);
      expect(
        genreSourceLabel(GenreSource.bpm, bpm: 123.6),
        'Gợi ý theo nhịp 124 BPM',
      );
      expect(genreSourceLabel(GenreSource.name), 'Gợi ý theo tên bài');
      expect(genreSourceLabel(GenreSource.saved), 'Đã chọn cho bài này');
    });

    test('tóm tắt preset', () {
      expect(fxSummary(const FxPreset()), 'Không hiệu ứng');
      expect(
        fxSummary(
          const FxPreset(
            reverbEnabled: true,
            reverbType: ReverbType.hall,
            reverbDecayS: 2.2,
            delayDivision: DelayDivision.quarter,
            compEnabled: true,
            compRatio: 3,
          ),
        ),
        'Vang hội trường 2,2 s · vọng 1/4 nốt · nén 3:1',
      );
      expect(
        fxSummary(const FxPreset(delayDivision: DelayDivision.dottedEighth)),
        'Vọng 1/8 chấm',
      );
    });

    test('mức chỉnh giọng kèm preset', () {
      expect(genreCorrectionLabel(genreCorrection(Genre.dry)), isNull);
      expect(genreCorrectionLabel(genreCorrection(Genre.rap)), 'Tắt');
      expect(
        genreCorrectionLabel(genreCorrection(Genre.bolero)),
        'Tự nhiên 45 ms',
      );
      expect(genreCorrectionLabel(genreCorrection(Genre.remix)), 'Ca sĩ 8 ms');
    });
  });

  test('FxPreset JSON đi về (không cần thư viện native)', () {
    const p = FxPreset(
      eqHighPassHz: 150,
      eqBands: [
        EqBand(type: EqBandType.bell, freqHz: 3000, gainDb: 2, q: 1),
        EqBand(type: EqBandType.highShelf, freqHz: 12000, gainDb: 2),
        EqBand(),
        EqBand(),
        EqBand(),
      ],
      compEnabled: true,
      delayDivision: DelayDivision.eighth,
      reverbEnabled: true,
      reverbType: ReverbType.plate,
    );
    expect(FxPreset.fromJson(p.toJson()), p);
    expect(FxPreset.fromJson(const {}), const FxPreset());
    expect(EqBandType.fromCode(5), EqBandType.highShelf);
    expect(EqBandType.fromCode(1), EqBandType.off); // mã lạ → tắt
  });

  group('nhớ preset theo bài', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('song_prefs_fx'));
    tearDown(() => dir.deleteSync(recursive: true));

    test('lưu cùng tone beat; đổi tone về 0 không mất preset', () async {
      final prefs = SongPrefs(baseDir: dir);
      expect(await prefs.genreFor(name: 'a.m4a', size: 10), isNull);
      await prefs.setTranspose(name: 'a.m4a', size: 10, semitones: 2);
      await prefs.setGenre(name: 'a.m4a', size: 10, genre: Genre.bolero);
      final again = SongPrefs(baseDir: dir);
      expect(await again.genreFor(name: 'a.m4a', size: 10), Genre.bolero);
      expect(await again.transposeFor(name: 'a.m4a', size: 10), 2);
      await again.setTranspose(name: 'a.m4a', size: 10, semitones: 0);
      expect(await again.genreFor(name: 'a.m4a', size: 10), Genre.bolero);
      expect(await again.transposeFor(name: 'a.m4a', size: 10), 0);
      expect(await again.genreFor(name: 'a.m4a', size: 11), isNull);
    });

    test('ghi liền nhau không đè nhau', () async {
      final prefs = SongPrefs(baseDir: dir);
      await Future.wait([
        prefs.setTranspose(name: 'b', size: 1, semitones: -3),
        prefs.setGenre(name: 'b', size: 1, genre: Genre.rap),
        prefs.setLastGenre(Genre.rap),
        prefs.setGenre(name: 'c', size: 1, genre: Genre.lofi),
      ]);
      expect(await prefs.transposeFor(name: 'b', size: 1), -3);
      expect(await prefs.genreFor(name: 'b', size: 1), Genre.rap);
      expect(await prefs.genreFor(name: 'c', size: 1), Genre.lofi);
      expect(await prefs.lastGenre(), Genre.rap);
    });

    test('preset gần nhất không bị bỏ khi đầy; tên preset lạ → null', () async {
      final prefs = SongPrefs(baseDir: dir);
      await prefs.setLastGenre(Genre.ballad);
      for (var i = 0; i <= SongPrefs.maxEntries; i++) {
        await prefs.setGenre(name: 's$i', size: 1, genre: Genre.pop);
      }
      expect(await prefs.lastGenre(), Genre.ballad);
      expect(await prefs.genreFor(name: 's0', size: 1), isNull);
      expect(await prefs.genreFor(name: 's1', size: 1), Genre.pop);

      File('${dir.path}/song_prefs.json')
          .writeAsStringSync('{"x|1": {"genre": "jazz"}, "*lastGenre": 3}');
      expect(await prefs.genreFor(name: 'x', size: 1), isNull);
      expect(await prefs.lastGenre(), isNull);
    });
  });

  group('khu hiệu ứng (Phòng thử)', () {
    Future<void> pump(
      WidgetTester tester, {
      required Genre genre,
      FxPreset preset = const FxPreset(),
      GenreSource? source,
      void Function(Genre)? onGenre,
      void Function(double)? onMix,
    }) async {
      // Bề ngang điện thoại nhỏ (iPhone SE): chip phải xuống dòng, không tràn.
      tester.view.physicalSize = const Size(320 * 3, 640 * 3);
      tester.view.devicePixelRatio = 3;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ListView(
              padding: const EdgeInsets.all(16),
              children: [
                FxPanel(
                  genre: genre,
                  preset: preset,
                  source: source,
                  bpm: 124,
                  mix: 1.5,
                  gateOn: false,
                  monitorOn: false,
                  onGenre: onGenre ?? (_) {},
                  onMix: onMix ?? (_) {},
                  onGate: (_) {},
                ),
              ],
            ),
          ),
        ),
      );
    }

    testWidgets('7 chip, bấm chip đổi preset', (tester) async {
      Genre? picked;
      await pump(
        tester,
        genre: Genre.pop,
        preset: const FxPreset(
          reverbEnabled: true,
          reverbType: ReverbType.plate,
          reverbDecayS: 1.4,
          delayDivision: DelayDivision.eighth,
          compEnabled: true,
          compRatio: 4,
        ),
        source: GenreSource.bpm,
        onGenre: (g) => picked = g,
      );
      for (final g in Genre.values) {
        expect(find.widgetWithText(ChoiceChip, genreLabel(g)), findsOneWidget);
      }
      final selected = tester.widget<ChoiceChip>(
        find.widgetWithText(ChoiceChip, 'Nhạc trẻ'),
      );
      expect(selected.selected, isTrue);
      expect(
        find.text('Vang plate 1,4 s · vọng 1/8 nốt · nén 4:1'),
        findsOneWidget,
      );
      expect(find.text('Kèm chỉnh giọng: Ca sĩ 15 ms'), findsOneWidget);
      expect(find.text('Gợi ý theo nhịp 124 BPM'), findsOneWidget);
      expect(find.text('150%'), findsOneWidget);
      expect(
        find.text('Bật "Nghe lại giọng" để nghe hiệu ứng khi hát.'),
        findsOneWidget,
      );
      expect(tester.widget<Slider>(find.byType(Slider)).onChanged, isNotNull);

      await tester.tap(find.widgetWithText(ChoiceChip, 'Rap'));
      expect(picked, Genre.rap);
    });

    testWidgets('Khô: không hiệu ứng, khoá thanh Vang', (tester) async {
      await pump(tester, genre: Genre.dry);
      expect(find.text('Không hiệu ứng'), findsOneWidget);
      expect(find.textContaining('Kèm chỉnh giọng'), findsNothing);
      expect(tester.widget<Slider>(find.byType(Slider)).onChanged, isNull);
    });
  });
}
