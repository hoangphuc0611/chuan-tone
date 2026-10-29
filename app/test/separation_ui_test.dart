// Giao diện khu tách giọng (file riêng: testWidgets thay HttpClient bằng bản giả trả 400 cho cả
// file, nên không để chung với test client HTTP).

import 'package:chuantone/engine/labels.dart';
import 'package:chuantone/engine/separation_client.dart';
import 'package:chuantone/widgets/separation_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('khu tách giọng (Phòng thử)', () {
    Future<void> pump(
      WidgetTester tester, {
      bool hasServer = true,
      bool hasStems = false,
      SeparationProgress? progress,
      String? error,
      double vocalGain = 0,
      void Function(double)? onVocalGain,
      VoidCallback? onSeparate,
    }) async {
      tester.view.physicalSize = const Size(320 * 3, 640 * 3);
      tester.view.devicePixelRatio = 3;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: ListView(
              padding: const EdgeInsets.all(16),
              children: [
                SeparationPanel(
                  hasServer: hasServer,
                  hasStems: hasStems,
                  progress: progress,
                  error: error,
                  lastResult: null,
                  vocalGain: vocalGain,
                  onSeparate: onSeparate ?? () {},
                  onCancel: () {},
                  onDelete: () {},
                  onVocalGain: onVocalGain ?? (_) {},
                ),
              ],
            ),
          ),
        ),
      );
    }

    testWidgets('chưa có máy chủ: nút tách bị khoá, có hướng dẫn', (
      tester,
    ) async {
      await pump(tester, hasServer: false);
      final button = tester.widget<FilledButton>(find.byType(FilledButton));
      expect(button.onPressed, isNull);
      expect(find.textContaining('địa chỉ máy chủ'), findsOneWidget);
    });

    testWidgets('bấm tách; đang tách hiện tiến độ + huỷ', (tester) async {
      var tapped = false;
      await pump(tester, onSeparate: () => tapped = true);
      await tester.tap(find.text('Tách giọng ca sĩ'));
      expect(tapped, isTrue);
      await pump(
        tester,
        progress: const SeparationProgress(
          stage: SeparationStage.uploading,
          fraction: 0.3,
          chunks: 12,
          uploaded: 3,
          downloaded: 0,
          elapsed: Duration(seconds: 5),
        ),
      );
      expect(find.text('Đang gửi bài 3/12… (5 s)'), findsOneWidget);
      expect(find.byType(LinearProgressIndicator), findsOneWidget);
      expect(find.text('Huỷ'), findsOneWidget);
    });

    testWidgets('đã tách: thanh "Giữ giọng ca sĩ gốc"', (tester) async {
      double? got;
      await pump(
        tester,
        hasStems: true,
        vocalGain: 0.4,
        onVocalGain: (v) => got = v,
      );
      expect(find.text('Giữ giọng ca sĩ gốc'), findsOneWidget);
      expect(find.text('40%'), findsOneWidget);
      await tester.drag(find.byType(Slider), const Offset(-200, 0));
      expect(got, isNotNull);
      expect(find.text('Xoá bản tách'), findsOneWidget);
    });

    test('nhãn kết quả', () {
      const r = SeparationResult(
        vocals: [],
        elapsed: Duration(milliseconds: 43400),
        serverSeconds: 38.2,
      );
      expect(separationDoneLabel(r), 'Tách xong sau 43 s (máy chủ tách 38 s)');
    });
  });
}
