import 'dart:typed_data';

import 'package:flutter/material.dart';

/// Hình sóng bản thu có vị trí phát (phần đã nghe tô màu chính) và vùng cắt đầu / đuôi (phần bị
/// cắt mờ đi). Chạm để tua; kéo thì vạch vị trí chạy theo tay, thả ra mới tua (như thanh trượt cũ —
/// không gọi tua engine liên tục). [position], [trimStart], [trimEnd] là phần 0..1 của bài.
class WaveformView extends StatefulWidget {
  const WaveformView({
    super.key,
    required this.levels,
    required this.position,
    this.trimStart = 0,
    this.trimEnd = 1,
    this.onSeek,
    this.height = 72,
  });

  final Float32List levels;
  final double position;
  final double trimStart;
  final double trimEnd;

  /// Phần 0..1 được chạm / kéo tới; null = không tua được.
  final ValueChanged<double>? onSeek;
  final double height;

  @override
  State<WaveformView> createState() => _WaveformViewState();
}

class _WaveformViewState extends State<WaveformView> {
  double? _drag; // vị trí đang kéo (chưa tua)

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final onSeek = widget.onSeek;
    final position = _drag ?? widget.position;
    return LayoutBuilder(
      builder: (context, box) {
        double frac(double dx) => (dx / box.maxWidth).clamp(0.0, 1.0);
        return Semantics(
          slider: true,
          label: 'Vị trí phát',
          value: '${(position * 100).round()}%',
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTapUp: onSeek == null
                ? null
                : (d) => onSeek(frac(d.localPosition.dx)),
            onHorizontalDragStart: onSeek == null
                ? null
                : (d) => setState(() => _drag = frac(d.localPosition.dx)),
            onHorizontalDragUpdate: onSeek == null
                ? null
                : (d) => setState(() => _drag = frac(d.localPosition.dx)),
            onHorizontalDragEnd: onSeek == null
                ? null
                : (_) {
                    final f = _drag;
                    setState(() => _drag = null);
                    if (f != null) onSeek(f);
                  },
            onHorizontalDragCancel: () => setState(() => _drag = null),
            child: CustomPaint(
              size: Size(box.maxWidth, widget.height),
              painter: _WaveformPainter(
                levels: widget.levels,
                position: position,
                trimStart: widget.trimStart,
                trimEnd: widget.trimEnd,
                played: cs.primary,
                rest: cs.onSurfaceVariant.withValues(alpha: 0.35),
                cut: cs.onSurfaceVariant.withValues(alpha: 0.12),
              ),
            ),
          ),
        );
      },
    );
  }
}

class _WaveformPainter extends CustomPainter {
  _WaveformPainter({
    required this.levels,
    required this.position,
    required this.trimStart,
    required this.trimEnd,
    required this.played,
    required this.rest,
    required this.cut,
  });

  final Float32List levels;
  final double position;
  final double trimStart;
  final double trimEnd;
  final Color played;
  final Color rest;
  final Color cut;

  @override
  void paint(Canvas canvas, Size size) {
    final n = levels.length;
    if (n == 0) return;
    const gap = 2.0;
    final w = (size.width - gap * (n - 1)) / n;
    final paint = Paint();
    for (var i = 0; i < n; i++) {
      final x = i * (w + gap);
      final mid = (i + 0.5) / n;
      paint.color = mid < trimStart || mid > trimEnd
          ? cut
          : mid <= position
          ? played
          : rest;
      // Cột thấp nhất vẫn thấy được (đoạn lặng).
      final h = (levels[i] * size.height).clamp(4.0, size.height);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(x, (size.height - h) / 2, w, h),
          Radius.circular(w / 2),
        ),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(_WaveformPainter old) =>
      old.levels != levels ||
      old.position != position ||
      old.trimStart != trimStart ||
      old.trimEnd != trimEnd ||
      old.played != played;
}
