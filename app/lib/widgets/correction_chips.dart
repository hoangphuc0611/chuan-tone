import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/labels.dart';
import 'app_ui.dart';

/// Mức chỉnh giọng Tắt / Tự nhiên / Ca sĩ / Robot — cùng một kiểu ở Màn hát và màn Nghe lại (xuống
/// dòng khi màn hẹp, chữ to). [hint]: dòng nhắc bên dưới (vd bật "Nghe lại giọng").
class CorrectionChips extends StatelessWidget {
  const CorrectionChips({
    super.key,
    required this.value,
    required this.onChanged,
    this.hint,
  });

  final CorrectionPreset? value;
  final ValueChanged<CorrectionPreset?> onChanged;
  final String? hint;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final options = <(String, CorrectionPreset?)>[
      ('Tắt', null),
      for (final p in CorrectionPreset.values) (correctionPresetLabel(p), p),
    ];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Chỉnh giọng', style: theme.textTheme.titleMedium),
        const SizedBox(height: 6),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (final (label, p) in options)
              AppChoiceChip(
                label: label,
                selected: value == p,
                onSelected: () => onChanged(p),
              ),
          ],
        ),
        if (hint case final h?)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(h, style: theme.textTheme.bodySmall),
          ),
      ],
    );
  }
}
