// Đếm phiên / thoát bất thường, nhật ký lỗi trên máy (M9 B5, docs/m9-plan.md).

import 'dart:io';

import 'package:chuantone/data/app_health.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory dir;
  setUp(() {
    AppHealth.reset();
    dir = Directory.systemTemp.createTempSync('health');
  });
  tearDown(() {
    AppHealth.reset();
    dir.deleteSync(recursive: true);
  });

  test('vào nền rồi tắt: không tính thoát bất thường', () async {
    await AppHealth.init(baseDir: dir);
    expect(AppHealth.stats.sessions, 1);
    await AppHealth.background();
    await AppHealth.foreground(); // quay lại từ nền = phiên mới
    await AppHealth.background();
    AppHealth.reset();

    await AppHealth.init(baseDir: dir);
    expect(AppHealth.stats.sessions, 3);
    expect(AppHealth.stats.abnormalExits, 0);
    expect(AppHealth.stats.crashFree, 1.0);
  });

  test(
    'app chết khi đang mở: lần mở sau tính một lần thoát bất thường',
    () async {
      await AppHealth.init(baseDir: dir);
      await AppHealth.flush();
      AppHealth.reset(); // như app bị giết, không qua background()

      await AppHealth.init(baseDir: dir);
      expect(AppHealth.stats.sessions, 2);
      expect(AppHealth.stats.abnormalExits, 1);
      expect(AppHealth.stats.crashFree, 0.5);
      expect(
        AppHealth.stats.summary,
        '2 phiên · 1 lần thoát bất thường · 0 lỗi',
      );
    },
  );

  test(
    'vào nền ngay lúc mở app (chưa đọc xong file): không mất số đếm cũ',
    () async {
      await AppHealth.init(baseDir: dir);
      await AppHealth.background();
      AppHealth.reset();
      // Không chờ init: background() đến ngay sau.
      final init = AppHealth.init(baseDir: dir);
      final bg = AppHealth.background();
      await Future.wait([init, bg]);
      expect(AppHealth.stats.sessions, 2);
      AppHealth.reset();
      await AppHealth.init(baseDir: dir);
      expect(AppHealth.stats.sessions, 3);
      expect(AppHealth.stats.abnormalExits, 0);
    },
  );

  test('nhật ký lỗi: ghi, gửi, quay vòng, xoá', () async {
    await AppHealth.init(baseDir: dir);
    AppHealth.recordError('Lỗi chưa bắt', 'StateError: hỏng\n#0 main');
    final report = await AppHealth.report();
    expect(report, contains('1 lỗi'));
    expect(report, contains('StateError: hỏng'));

    final big = 'x' * 2000;
    for (var i = 0; i < 200; i++) {
      AppHealth.recordError('Lỗi giao diện', '$i $big');
    }
    await AppHealth.flush();
    final size = await AppHealth.logFile.length();
    expect(size, lessThanOrEqualTo(AppHealth.maxLogBytes + 4096));
    final text = await AppHealth.logFile.readAsString();
    expect(text, startsWith('[')); // cắt ở đầu một mục
    expect(text, contains('199 x')); // giữ phần mới nhất
    expect(AppHealth.stats.errors, 201);

    await AppHealth.clear();
    expect(await AppHealth.logFile.exists(), isFalse);
    expect(AppHealth.stats.errors, 0);
    expect(AppHealth.stats.sessions, 1);
    expect(await AppHealth.report(), contains('Chưa có lỗi nào'));
  });
}
