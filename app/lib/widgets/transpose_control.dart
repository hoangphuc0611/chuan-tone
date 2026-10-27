import 'package:flutter/material.dart';

import '../engine/labels.dart';

/// Đổi tone beat −6…+6: hai nút to − / +, mức hiện tại ở giữa, và tiến độ khi engine đang render
/// bản mới (nghe được sau ~0,1 s, cả bài xong sau vài giây ở luồng nền).
class TransposeControl extends StatelessWidget {
  const TransposeControl({
    super.key,
    required this.value,
    required this.audible,
    required this.progress,
    required this.onChanged,
    this.enabled = true,
  });

  static const minValue = -6;
  static const maxValue = 6;

  /// Mức đã chọn.
  final int value;

  /// Mức đang nghe (khác [value] trong lúc chờ bản mới).
  final int audible;

  /// 0..1: phần bài đã render ở [value].
  final double progress;
  final ValueChanged<int> onChanged;
  final bool enabled;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final pending = value != audible;
    final rendering = value != 0 && progress < 1.0;
    final buttonStyle = IconButton.styleFrom(minimumSize: const Size(56, 56));
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Expanded(
              child: Text('Tone beat', style: theme.textTheme.titleMedium),
            ),
            IconButton.filledTonal(
              tooltip: 'Hạ tone',
              style: buttonStyle,
              iconSize: 32,
              onPressed: enabled && value > minValue
                  ? () => onChanged(value - 1)
                  : null,
              icon: const Icon(Icons.remove),
            ),
            SizedBox(
              width: 76,
              child: Text(
                transposeLabel(value),
                key: const ValueKey('transpose-value'),
                textAlign: TextAlign.center,
                style: theme.textTheme.headlineSmall?.copyWith(
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
            IconButton.filledTonal(
              tooltip: 'Nâng tone',
              style: buttonStyle,
              iconSize: 32,
              onPressed: enabled && value < maxValue
                  ? () => onChanged(value + 1)
                  : null,
              icon: const Icon(Icons.add),
            ),
          ],
        ),
        if (value != 0 && !rendering && !pending)
          Text(
            'Đã ${value > 0 ? 'nâng' : 'hạ'} ${value.abs()} nửa cung, giữ nguyên nhịp.',
            style: theme.textTheme.bodySmall,
          ),
        if (rendering || pending) ...[
          const SizedBox(height: 6),
          LinearProgressIndicator(value: pending ? null : progress),
          const SizedBox(height: 2),
          Text(
            pending
                ? 'Đang đổi tone…'
                : 'Đang chuẩn bị cả bài: ${(progress * 100).round()}%',
            style: theme.textTheme.bodySmall,
          ),
        ],
      ],
    );
  }
}
