import 'package:flutter/material.dart';

/// Một bong bóng mẹo chỉ vào [target].
class CoachMark {
  const CoachMark({
    required this.target,
    required this.title,
    required this.text,
  });

  final GlobalKey target;
  final String title;
  final String text;
}

/// Lần lượt hiện các bong bóng mẹo (M8: lần đầu vào Màn hát): làm tối màn hình, khoét sáng chỗ
/// [CoachMark.target], bong bóng ở dưới (hoặc trên nếu không đủ chỗ). "Tiếp" sang mẹo sau, "Bỏ qua"
/// tắt hết. Mẹo có target không hiện trên màn hình thì bỏ qua.
Future<void> showCoachMarks(BuildContext context, List<CoachMark> marks) async {
  for (var i = 0; i < marks.length; i++) {
    final m = marks[i];
    final ctx = m.target.currentContext;
    if (ctx == null || !ctx.mounted) continue;
    await Scrollable.ensureVisible(
      ctx,
      alignment: 0.3,
      duration: const Duration(milliseconds: 200),
    );
    if (!context.mounted || !ctx.mounted) return;
    final box = ctx.findRenderObject();
    if (box is! RenderBox || !box.hasSize) continue;
    final rect = box.localToGlobal(Offset.zero) & box.size;
    final next = await Navigator.of(context).push<bool>(
      _CoachRoute(mark: m, rect: rect, index: i, count: marks.length),
    );
    if (next != true) return;
  }
}

class _CoachRoute extends PopupRoute<bool> {
  _CoachRoute({
    required this.mark,
    required this.rect,
    required this.index,
    required this.count,
  });

  final CoachMark mark;
  final Rect rect;
  final int index;
  final int count;

  @override
  Color? get barrierColor => null;
  @override
  bool get barrierDismissible => false;
  @override
  String? get barrierLabel => 'Mẹo';
  @override
  Duration get transitionDuration => const Duration(milliseconds: 150);

  @override
  Widget buildPage(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
  ) {
    return FadeTransition(
      opacity: animation,
      child: _CoachOverlay(
        mark: mark,
        rect: rect,
        last: index == count - 1,
        step: '${index + 1}/$count',
      ),
    );
  }
}

class _CoachOverlay extends StatelessWidget {
  const _CoachOverlay({
    required this.mark,
    required this.rect,
    required this.last,
    required this.step,
  });

  final CoachMark mark;
  final Rect rect;
  final bool last;
  final String step;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final size = MediaQuery.sizeOf(context);
    final hole = rect.inflate(6);
    // Bong bóng dưới chỗ được chỉ nếu phía dưới rộng hơn, không thì phía trên.
    final below = size.height - hole.bottom >= hole.top;
    return Material(
      type: MaterialType.transparency,
      child: Stack(
        children: [
          Positioned.fill(
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: () => Navigator.pop(context, true),
              child: CustomPaint(
                painter: _HolePainter(
                  hole: hole,
                  color: Colors.black.withValues(alpha: 0.6),
                  ring: theme.colorScheme.primary,
                ),
              ),
            ),
          ),
          Positioned(
            left: 16,
            right: 16,
            top: below ? hole.bottom + 12 : null,
            bottom: below ? null : size.height - hole.top + 12,
            child: SafeArea(
              top: false,
              bottom: below,
              child: Card(
                elevation: 2,
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(16, 14, 8, 4),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        mark.title,
                        style: theme.textTheme.titleLarge?.copyWith(
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Padding(
                        padding: const EdgeInsets.only(right: 8),
                        child: Text(
                          mark.text,
                          style: theme.textTheme.bodyLarge,
                        ),
                      ),
                      Wrap(
                        alignment: WrapAlignment.end,
                        crossAxisAlignment: WrapCrossAlignment.center,
                        children: [
                          if (!last)
                            TextButton(
                              onPressed: () => Navigator.pop(context, false),
                              child: const Text('Bỏ qua'),
                            ),
                          TextButton(
                            onPressed: () => Navigator.pop(context, true),
                            child: Text(last ? 'Đã hiểu' : 'Tiếp ($step)'),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _HolePainter extends CustomPainter {
  const _HolePainter({
    required this.hole,
    required this.color,
    required this.ring,
  });

  final Rect hole;
  final Color color;
  final Color ring;

  @override
  void paint(Canvas canvas, Size size) {
    final r = RRect.fromRectAndRadius(hole, const Radius.circular(12));
    final path = Path()
      ..fillType = PathFillType.evenOdd
      ..addRect(Offset.zero & size)
      ..addRRect(r);
    canvas.drawPath(path, Paint()..color = color);
    canvas.drawRRect(
      r,
      Paint()
        ..color = ring
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3,
    );
  }

  @override
  bool shouldRepaint(_HolePainter old) =>
      old.hole != hole || old.color != color || old.ring != ring;
}
