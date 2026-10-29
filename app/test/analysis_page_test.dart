import 'dart:io';

import 'package:chuantone/data/song_prefs.dart';
import 'package:chuantone/widgets/analysis_widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

SongAnalysis analysis({
  MusicKey? key,
  double? bpm = 92,
  List<KeySegment>? timeline,
}) => SongAnalysis(
  duration: const Duration(minutes: 4),
  sampleRate: 44100,
  channels: 2,
  key: key,
  keyConfidence: 0.8,
  tuningCents: 0,
  bpm: bpm,
  bpmConfidence: 0.7,
  integratedLufs: -12,
  samplePeakDb: -0.5,
  timeline: timeline ?? const [],
);

Future<void> pumpPhone(WidgetTester tester, Widget child) async {
  tester.view.physicalSize = const Size(320 * 3, 640 * 3);
  tester.view.devicePixelRatio = 3;
  addTearDown(tester.view.reset);
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: ListView(padding: const EdgeInsets.all(16), children: [child]),
      ),
    ),
  );
}

void main() {
  testWidgets('các bước: đang chạy có thanh tiến độ, xong có dấu tích', (
    tester,
  ) async {
    await pumpPhone(
      tester,
      const AnalysisSteps(
        steps: [
          AnalysisStep(
            title: 'Tách giọng ca sĩ',
            state: StepStatus.skipped,
            subtitle: 'Không bắt buộc — dùng cho bài gốc có giọng ca sĩ.',
            action: FilledButton.tonal(onPressed: null, child: Text('Tách')),
          ),
          AnalysisStep(
            title: 'Dò tone',
            state: StepStatus.running,
            subtitle: 'Đang dò tone…',
            progress: 0.6,
          ),
          AnalysisStep(title: 'Dò nhịp', state: StepStatus.waiting),
        ],
      ),
    );
    expect(find.text('Dò tone'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    expect(find.text('Tách'), findsOneWidget);
    expect(find.byIcon(Icons.radio_button_unchecked), findsOneWidget);
  });

  testWidgets('kết quả: tone + BPM, chuyển tone, tone sửa tay', (tester) async {
    const am = MusicKey(9, MusicScale.naturalMinor);
    const a = MusicKey(9, MusicScale.major);
    var edits = 0;
    await pumpPhone(
      tester,
      KeyResultCard(
        analysis: analysis(
          key: am,
          timeline: const [
            KeySegment(
              start: Duration.zero,
              end: Duration(minutes: 3),
              key: am,
            ),
            KeySegment(
              start: Duration(minutes: 3),
              end: Duration(minutes: 4),
              key: a,
            ),
          ],
        ),
        manualKey: null,
        onEdit: () => edits++,
      ),
    );
    expect(find.text('Tone: La thứ (Am)'), findsOneWidget);
    expect(find.text('92 BPM'), findsOneWidget);
    expect(
      find.textContaining('Chuyển tone: 0:00 Am → 3:00 A'),
      findsOneWidget,
    );
    await tester.tap(find.text('Sửa tone'));
    expect(edits, 1);

    await pumpPhone(
      tester,
      KeyResultCard(
        analysis: analysis(key: am),
        manualKey: const MusicKey(2, MusicScale.major),
        onEdit: () {},
      ),
    );
    expect(find.text('Tone: Rê trưởng (D)'), findsOneWidget);
    expect(find.text('Đã sửa tay (máy dò: La thứ (Am))'), findsOneWidget);
  });

  testWidgets('hộp sửa tone: chọn Rê thứ rồi lưu; "Theo máy dò" khi đã sửa', (
    tester,
  ) async {
    KeyEdit? result;
    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (ctx) => TextButton(
            onPressed: () async => result = await showKeyEditor(
              ctx,
              initial: const MusicKey(9, MusicScale.naturalMinor),
              canReset: true,
            ),
            child: const Text('mở'),
          ),
        ),
      ),
    );
    await tester.tap(find.text('mở'));
    await tester.pumpAndSettle();
    expect(find.text('La thứ (Am)'), findsOneWidget);
    await tester.tap(find.byType(DropdownButton<int>));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Rê (D)').last);
    await tester.pumpAndSettle();
    expect(find.text('Rê thứ (Dm)'), findsOneWidget);
    await tester.tap(find.text('Lưu'));
    await tester.pumpAndSettle();
    expect(result!.reset, isFalse);
    expect(result!.key, const MusicKey(2, MusicScale.naturalMinor));

    await tester.tap(find.text('mở'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Theo máy dò'));
    await tester.pumpAndSettle();
    expect(result!.reset, isTrue);
  });

  group('nhớ tone sửa tay', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('song_key'));
    tearDown(() => dir.deleteSync(recursive: true));

    test('lưu, đọc lại, xoá; không đụng tone beat / preset', () async {
      final prefs = SongPrefs(baseDir: dir);
      expect(await prefs.keyFor(name: 'a', size: 1), isNull);
      await prefs.setTranspose(name: 'a', size: 1, semitones: 2);
      await prefs.setKey(
        name: 'a',
        size: 1,
        key: const MusicKey(7, MusicScale.major),
      );
      final again = SongPrefs(baseDir: dir);
      expect(
        await again.keyFor(name: 'a', size: 1),
        const MusicKey(7, MusicScale.major),
      );
      expect(await again.transposeFor(name: 'a', size: 1), 2);
      await again.setKey(name: 'a', size: 1, key: null);
      expect(await again.keyFor(name: 'a', size: 1), isNull);
      expect(await again.transposeFor(name: 'a', size: 1), 2);
    });

    test('giá trị hỏng → null', () async {
      File('${dir.path}/song_prefs.json').writeAsStringSync(
        '{"a|1": {"key": {"root": 14, "scale": "major"}}, "b|1": {"key": {"root": 3, "scale": "x"}}}',
      );
      final prefs = SongPrefs(baseDir: dir);
      expect(await prefs.keyFor(name: 'a', size: 1), isNull);
      expect(await prefs.keyFor(name: 'b', size: 1), isNull);
    });
  });
}
