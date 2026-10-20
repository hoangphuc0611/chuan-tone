// Test giao diện không cần thư viện native: truyền EngineInfo giả vào app.

import 'package:chuantone/app.dart';
import 'package:chuantone/engine_info.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('hiển thị trạng thái engine OK', (tester) async {
    await tester.pumpWidget(
      const ChuanToneApp(
        engineInfo: EngineInfo(
          ok: true,
          version: '0.1.0',
          apiVersion: '0.1.0',
          framesProcessed: 256,
        ),
      ),
    );
    expect(find.text('ChuẩnTone'), findsOneWidget);
    expect(find.text('Bấm bài là hát'), findsOneWidget);
    expect(find.text('ToneCore v0.1.0 – engine OK'), findsOneWidget);
  });

  testWidgets('hiển thị lỗi khi engine hỏng', (tester) async {
    await tester.pumpWidget(
      const ChuanToneApp(
        engineInfo: EngineInfo(
          ok: false,
          error: 'Không tìm thấy libtonecore.so',
        ),
      ),
    );
    expect(find.text('Không khởi động được engine'), findsOneWidget);
    expect(find.text('Không tìm thấy libtonecore.so'), findsOneWidget);
  });
}
