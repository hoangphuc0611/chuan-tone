import 'package:flutter/material.dart';

/// Thanh mức micro (M8 B4): đỉnh micro −60…0 dBFS. Xanh khi vừa, cam khi to, đỏ khi sát vỡ tiếng.
class InputLevelMeter extends StatelessWidget {
  const InputLevelMeter({super.key, required this.peakDb});

  /// Đỉnh micro (dBFS) trong ~300 ms gần nhất.
  final double peakDb;

  static const floorDb = -60.0;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final f = ((peakDb - floorDb) / -floorDb).clamp(0.0, 1.0);
    final color = peakDb > -3
        ? theme.colorScheme.error
        : peakDb > -12
        ? theme.colorScheme.secondary
        : theme.colorScheme.tertiary;
    return Semantics(
      label: 'Mức micro',
      value: '${peakDb.round()} dB',
      child: ClipRRect(
        borderRadius: BorderRadius.circular(6),
        child: SizedBox(
          height: 14,
          child: Stack(
            fit: StackFit.expand,
            children: [
              ColoredBox(color: theme.colorScheme.surfaceContainerHighest),
              FractionallySizedBox(
                alignment: Alignment.centerLeft,
                widthFactor: f,
                child: ColoredBox(color: color),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
