import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/labels.dart';

/// Đồng hồ cao độ: nốt đang hát + kim lệch ±50 cent so với nốt đích.
/// Xanh khi lệch ≤ 10 cent, cam khi lệch hơn.
class PitchMeter extends StatelessWidget {
  const PitchMeter({super.key, required this.state});

  final PitchState? state;

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
    } else {
      hint = s.centsOff > 0
          ? 'Cao ${s.centsOff.round()} cent'
          : 'Thấp ${(-s.centsOff).round()} cent';
    }

    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(
          children: [
            Text(
              active ? midiNoteLabel(s.targetNote!) : '—',
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
            SizedBox(
              height: 28,
              child: LayoutBuilder(
                builder: (context, c) {
                  final w = c.maxWidth;
                  final x = (cents + 50) / 100 * w;
                  return Stack(
                    clipBehavior: Clip.none,
                    children: [
                      // Thang −50 … +50 cent, vùng xanh ±10 cent ở giữa.
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
                        child: ColoredBox(
                          color: Colors.green.withValues(alpha: 0.35),
                        ),
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
            ),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('Thấp', style: theme.textTheme.bodySmall),
                Text('Cao', style: theme.textTheme.bodySmall),
              ],
            ),
            if (active && s.enabled && s.correctionCents.abs() >= 1)
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
