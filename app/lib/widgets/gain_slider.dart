import 'package:flutter/material.dart';

/// Thanh trượt âm lượng / mức: nhãn, thanh, phần trăm.
class GainSlider extends StatelessWidget {
  const GainSlider({
    super.key,
    required this.label,
    required this.value,
    required this.onChanged,
    this.max = 1,
  });

  final String label;
  final double value;
  final double max;

  /// null = khoá thanh trượt.
  final ValueChanged<double>? onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(width: 120, child: Text(label)),
        Expanded(
          child: Slider(value: value, max: max, onChanged: onChanged),
        ),
        SizedBox(width: 44, child: Text('${(value * 100).round()}%')),
      ],
    );
  }
}
