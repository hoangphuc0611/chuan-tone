import 'dart:io';

import 'package:chuantone/data/analysis_cache.dart';
import 'package:chuantone/engine/labels.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

const _am = MusicKey(9, MusicScale.naturalMinor);
const _a = MusicKey(9, MusicScale.major);

SongAnalysis _analysis({
  MusicKey? key = _am,
  double? bpm = 91.6,
  double? tuning = 18.4,
  List<KeySegment>? timeline,
}) => SongAnalysis(
  duration: const Duration(minutes: 3, seconds: 30),
  sampleRate: 44100,
  channels: 2,
  key: key,
  keyConfidence: 0.8,
  tuningCents: tuning,
  bpm: bpm,
  bpmConfidence: 0.7,
  integratedLufs: -14.2,
  samplePeakDb: -0.3,
  timeline:
      timeline ??
      const [
        KeySegment(
          start: Duration.zero,
          end: Duration(seconds: 95),
          key: _am,
          confidence: 0.9,
        ),
        KeySegment(
          start: Duration(seconds: 95),
          end: Duration(seconds: 160),
          key: _a,
          confidence: 0.7,
        ),
        KeySegment(
          start: Duration(seconds: 160),
          end: Duration(seconds: 210),
          key: _am,
          confidence: 0.8,
        ),
      ],
);

void main() {
  group('nhãn phân tích bài', () {
    test('tóm tắt: tone – BPM', () {
      expect(analysisSummary(_analysis()), 'La thứ (Am) – 92 BPM');
      expect(analysisSummary(_analysis(bpm: null)), 'La thứ (Am)');
      expect(
        analysisSummary(_analysis(key: null)),
        'Chưa dò được tone – 92 BPM',
      );
    });

    test('chuyển tone', () {
      expect(modulationSummary(_analysis()), '0:00 Am → 1:35 A → 2:40 Am');
      final single = _analysis(
        timeline: const [
          KeySegment(start: Duration.zero, end: Duration(minutes: 3), key: _am),
        ],
      );
      expect(modulationSummary(single), isNull);
    });

    test('tuning chỉ nhắc khi lệch rõ', () {
      expect(tuningNote(_analysis()), contains('+18 cent'));
      expect(tuningNote(_analysis(tuning: -7.6)), contains('−8 cent'));
      expect(tuningNote(_analysis(tuning: 2)), isNull);
      expect(tuningNote(_analysis(tuning: null)), isNull);
    });

    test('giai đoạn và ký hiệu tone', () {
      expect(analysisStageLabel(AnalysisStage.key), 'Đang dò tone…');
      expect(keySymbol(const MusicKey(6, MusicScale.naturalMinor)), 'F#m');
      expect(keySymbol(const MusicKey(0, MusicScale.major)), 'C');
    });
  });

  group('bộ nhớ kết quả phân tích', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('analysis_cache'));
    tearDown(() => dir.deleteSync(recursive: true));

    test('lưu và đọc lại đủ các trường', () async {
      final cache = AnalysisCache(engineVersion: '0.4.0', baseDir: dir);
      expect(await cache.lookup(name: 'bai.mp3', size: 100), isNull);
      await cache.store(name: 'bai.mp3', size: 100, analysis: _analysis());
      final got = await AnalysisCache(
        engineVersion: '0.4.0',
        baseDir: dir,
      ).lookup(name: 'bai.mp3', size: 100);
      expect(got, isNotNull);
      expect(got!.key, _am);
      expect(got.bpm, closeTo(91.6, 1e-9));
      expect(got.tuningCents, closeTo(18.4, 1e-9));
      expect(got.integratedLufs, closeTo(-14.2, 1e-9));
      expect(got.duration, const Duration(minutes: 3, seconds: 30));
      expect(got.timeline.map((s) => s.key), [_am, _a, _am]);
      expect(got.timeline[1].start, const Duration(seconds: 95));
      expect(got.timeline[1].confidence, closeTo(0.7, 1e-9));
    });

    test(
      'khác kích thước hoặc khác phiên bản engine → phân tích lại',
      () async {
        await AnalysisCache(
          engineVersion: '0.4.0',
          baseDir: dir,
        ).store(name: 'bai.mp3', size: 100, analysis: _analysis());
        expect(
          await AnalysisCache(
            engineVersion: '0.4.0',
            baseDir: dir,
          ).lookup(name: 'bai.mp3', size: 101),
          isNull,
        );
        expect(
          await AnalysisCache(
            engineVersion: '0.5.0',
            baseDir: dir,
          ).lookup(name: 'bai.mp3', size: 100),
          isNull,
        );
      },
    );

    test('kết quả không có tone / BPM / tuning vẫn lưu được', () async {
      final cache = AnalysisCache(engineVersion: '0.4.0', baseDir: dir);
      await cache.store(
        name: 'trong.wav',
        size: 1,
        analysis: _analysis(key: null, bpm: null, tuning: null, timeline: []),
      );
      final got = await cache.lookup(name: 'trong.wav', size: 1);
      expect(got!.key, isNull);
      expect(got.bpm, isNull);
      expect(got.tuningCents, isNull);
      expect(got.timeline, isEmpty);
    });

    test('giữ tối đa 200 bài, bỏ bài cũ nhất', () async {
      final cache = AnalysisCache(engineVersion: '0.4.0', baseDir: dir);
      for (var i = 0; i < AnalysisCache.maxEntries + 3; i++) {
        await cache.store(name: 'b$i', size: i, analysis: _analysis());
      }
      expect(await cache.lookup(name: 'b0', size: 0), isNull);
      expect(await cache.lookup(name: 'b3', size: 3), isNotNull);
    });

    test('file hỏng → coi như chưa có', () async {
      File('${dir.path}/analysis_cache.json').writeAsStringSync('{hong');
      final cache = AnalysisCache(engineVersion: '0.4.0', baseDir: dir);
      expect(await cache.lookup(name: 'x', size: 1), isNull);
      await cache.store(name: 'x', size: 1, analysis: _analysis());
      expect(await cache.lookup(name: 'x', size: 1), isNotNull);
    });
  });
}
