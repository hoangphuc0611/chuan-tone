import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme.dart';

/// Đồng hồ kim nửa vòng cho kết quả đo độ trễ: vùng xanh ≤ 35 ms (hát mượt), vàng tới 60 ms (hơi
/// vọng), đỏ hơn nữa (nên tắt nghe lại giọng) — cùng ngưỡng với `latencyVerdict`. Thang 0…100 ms.
class LatencyGauge extends StatelessWidget {
  const LatencyGauge({super.key, required this.ms});

  final double ms;

  static const maxMs = 100.0;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    return Semantics(
      label: 'Độ trễ ${ms.round()} mili giây',
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: 220,
            height: 118,
            child: CustomPaint(
              painter: _GaugePainter(
                ms: ms,
                zones: [goodGreen, warnOrange, cs.error],
                needle: cs.onSurface,
              ),
            ),
          ),
          Text.rich(
            TextSpan(
              children: [
                TextSpan(
                  text: '${ms.round()}',
                  style: theme.textTheme.headlineMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
                ),
                TextSpan(text: ' ms', style: theme.textTheme.titleMedium),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  _GaugePainter({required this.ms, required this.zones, required this.needle});

  final double ms;
  final List<Color> zones;
  final Color needle;

  @override
  void paint(Canvas canvas, Size size) {
    const stroke = 16.0;
    final r = math.min(size.width / 2, size.height) - stroke / 2;
    final c = Offset(size.width / 2, size.height - 8);
    final rect = Rect.fromCircle(center: c, radius: r);
    // Góc: π (trái, 0 ms) → 2π (phải, 100 ms).
    double angle(double v) =>
        math.pi + math.pi * (v / LatencyGauge.maxMs).clamp(0.0, 1.0);
    final bounds = [0.0, 35.0, 60.0, LatencyGauge.maxMs];
    final arc = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = stroke
      ..strokeCap = StrokeCap.butt;
    for (var i = 0; i < 3; i++) {
      arc.color = zones[i];
      final a0 = angle(bounds[i]);
      canvas.drawArc(rect, a0, angle(bounds[i + 1]) - a0, false, arc);
    }
    final a = angle(ms);
    final tip = c + Offset(math.cos(a), math.sin(a)) * (r - stroke);
    canvas.drawLine(
      c,
      tip,
      Paint()
        ..color = needle
        ..strokeWidth = 4
        ..strokeCap = StrokeCap.round,
    );
    canvas.drawCircle(c, 7, Paint()..color = needle);
  }

  @override
  bool shouldRepaint(_GaugePainter old) => old.ms != ms || old.zones != zones;
}
