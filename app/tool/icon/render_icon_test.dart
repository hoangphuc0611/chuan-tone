// Vẽ icon ChuẩnTone bằng code (M8 B5) — micro + sóng âm trắng trên nền đỏ chuyển màu; không dùng
// hình của ai. Chạy rồi chạy tool/icon/make_icons.sh để tạo các cỡ iOS / Android + màn khởi động:
//   fvm flutter test tool/icon/render_icon_test.dart
// Ra: build/icon/{icon_1024.png, foreground_432.png, legacy_192.png, splash_384.png}

import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

const _top = Color(0xFFF05A60);
const _bottom = Color(0xFFB4222D);

/// Micro + 2 cặp sóng âm, trong hệ toạ độ 1024 × 1024 (tâm 512, 512), màu [color].
void paintGlyph(Canvas c, Color color) {
  final fill = Paint()..color = color;
  final stroke = Paint()
    ..color = color
    ..style = PaintingStyle.stroke
    ..strokeWidth = 46
    ..strokeCap = StrokeCap.round;
  // Đầu micro.
  c.drawRRect(
    RRect.fromRectAndRadius(
      const Rect.fromLTWH(397, 222, 230, 360),
      const Radius.circular(115),
    ),
    fill,
  );
  // Tay đỡ chữ U, thân, đế.
  c.drawArc(
    Rect.fromCircle(center: const Offset(512, 452), radius: 190),
    0,
    math.pi,
    false,
    stroke,
  );
  c.drawLine(const Offset(512, 642), const Offset(512, 752), stroke);
  c.drawLine(const Offset(412, 774), const Offset(612, 774), stroke);
  // Sóng âm hai bên.
  for (final (r, alpha) in [(300.0, 0.9), (380.0, 0.6)]) {
    final wave = Paint()
      ..color = color.withValues(alpha: alpha)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 34
      ..strokeCap = StrokeCap.round;
    final rect = Rect.fromCircle(center: const Offset(512, 402), radius: r);
    c.drawArc(rect, -0.42, 0.84, false, wave);
    c.drawArc(rect, math.pi - 0.42, 0.84, false, wave);
  }
}

void paintBackground(Canvas c, double size) {
  c.drawRect(
    Rect.fromLTWH(0, 0, size, size),
    Paint()
      ..shader = ui.Gradient.linear(Offset.zero, Offset(size, size), [
        _top,
        _bottom,
      ]),
  );
}

Future<void> save(
  String path,
  int size,
  void Function(Canvas c, double s) draw,
) async {
  final rec = ui.PictureRecorder();
  final canvas = Canvas(rec);
  draw(canvas, size.toDouble());
  final img = await rec.endRecording().toImage(size, size);
  final png = await img.toByteData(format: ui.ImageByteFormat.png);
  File(path)
    ..createSync(recursive: true)
    ..writeAsBytesSync(png!.buffer.asUint8List());
}

void main() {
  test('vẽ icon', () async {
    const out = 'build/icon';
    // iOS: vuông, kín nền (iOS tự bo góc).
    await save('$out/icon_1024.png', 1024, (c, s) {
      paintBackground(c, s);
      paintGlyph(c, Colors.white);
    });
    // Android adaptive: lớp trước trong suốt, hình nằm trong vùng an toàn 66% ở giữa.
    await save('$out/foreground_432.png', 432, (c, s) {
      c.scale(s / 1024);
      c.translate(512, 512);
      c.scale(0.62);
      c.translate(-512, -512);
      paintGlyph(c, Colors.white);
    });
    // Android cũ (trước 8.0): hình tròn chuyển màu.
    await save('$out/legacy_192.png', 192, (c, s) {
      c.clipPath(Path()..addOval(Rect.fromLTWH(0, 0, s, s)));
      paintBackground(c, s);
      c.scale(s / 1024);
      c.translate(512, 512);
      c.scale(0.78);
      c.translate(-512, -512);
      paintGlyph(c, Colors.white);
    });
    // Màn khởi động: hình trắng trên nền trong suốt (nền đỏ do màn khởi động vẽ).
    await save('$out/splash_384.png', 384, (c, s) {
      c.scale(s / 1024);
      paintGlyph(c, Colors.white);
    });
  });
}
