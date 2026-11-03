import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/labels.dart';

/// Đồng hồ cao độ: nốt đang hát + kim lệch ±50 cent so với nốt đích.
/// Xanh khi lệch ≤ 10 cent, cam khi lệch hơn.
///
/// [compact]: một hàng thấp (Màn hát — tone bài phải là chữ to nhất màn hình).
class PitchMeter extends StatelessWidget {
  const PitchMeter({super.key, required this.state, this.compact = false});

  final PitchState? state;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = state;
    final active = s != null && s.voiced && s.targetNote != null;
    final cents = active ? s.centsOff.clamp(-50.0, 50.0) : 0.0;
    final inTune = active && s.centsOff.abs() <= 10;
    final color = !active
        ? theme.colorScheme.outline
        : inTune
        ? Colors.green.shade600
        : Colors.orange.shade700;

    final String hint;
    if (!active) {
      hint = 'Hãy hát để xem cao độ';
    } else if (inTune) {
      hint = 'Đúng tone';
    } else if (compact) {
      // Màn hát: lời thường, không dùng đơn vị "cent" (bảng thuật ngữ M8).
      final far = s.centsOff.abs() > 25;
      hint = s.centsOff > 0
          ? (far ? 'Cao quá' : 'Hơi cao')
          : (far ? 'Thấp quá' : 'Hơi thấp');
    } else {
      hint = s.centsOff > 0
          ? 'Cao ${s.centsOff.round()} cent'
          : 'Thấp ${(-s.centsOff).round()} cent';
    }
    final note = active ? midiNoteLabel(s.targetNote!) : '—';
    final correcting = active && s.enabled && s.correctionCents.abs() >= 1;
    final bar = _CentsBar(cents: cents, active: active, color: color);
    final scale = Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text('Thấp', style: theme.textTheme.bodySmall),
        Text('Cao', style: theme.textTheme.bodySmall),
      ],
    );

    if (compact) {
      return Card(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
          child: Row(
            children: [
              ConstrainedBox(
                constraints: const BoxConstraints(minWidth: 64),
                child: Text(
                  note,
                  textAlign: TextAlign.center,
                  style: theme.textTheme.headlineSmall?.copyWith(
                    color: color,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Text(
                      hint,
                      style: theme.textTheme.titleSmall?.copyWith(color: color),
                    ),
                    bar,
                    scale,
                  ],
                ),
              ),
            ],
          ),
        ),
      );
    }

    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(
          children: [
            Text(
              note,
              style: theme.textTheme.displayMedium?.copyWith(
                color: color,
                fontWeight: FontWeight.w700,
              ),
            ),
            Text(
              hint,
              style: theme.textTheme.titleMedium?.copyWith(color: color),
            ),
            const SizedBox(height: 12),
            bar,
            scale,
            if (correcting)
              Text(
                'Đang chỉnh ${s.correctionCents > 0 ? '+' : ''}'
                '${s.correctionCents.round()} cent',
                style: theme.textTheme.bodySmall,
              ),
          ],
        ),
      ),
    );
  }
}

/// Thang −50 … +50 cent, vùng xanh ±10 cent ở giữa, chấm tròn ở mức lệch.
class _CentsBar extends StatelessWidget {
  const _CentsBar({
    required this.cents,
    required this.active,
    required this.color,
  });

  final double cents;
  final bool active;
  final Color color;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return SizedBox(
      height: 28,
      child: LayoutBuilder(
        builder: (context, c) {
          final w = c.maxWidth;
          final x = (cents + 50) / 100 * w;
          return Stack(
            clipBehavior: Clip.none,
            children: [
              Positioned(
                left: 0,
                right: 0,
                top: 11,
                height: 6,
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    color: theme.colorScheme.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(3),
                  ),
                ),
              ),
              Positioned(
                left: w * 0.4,
                width: w * 0.2,
                top: 11,
                height: 6,
                child: ColoredBox(color: Colors.green.withValues(alpha: 0.35)),
              ),
              Positioned(
                left: w / 2 - 1,
                width: 2,
                top: 4,
                height: 20,
                child: ColoredBox(color: theme.colorScheme.outline),
              ),
              if (active)
                Positioned(
                  left: x - 7,
                  top: 7,
                  width: 14,
                  height: 14,
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: color,
                      shape: BoxShape.circle,
                    ),
                  ),
                ),
            ],
          );
        },
      ),
    );
  }
}
