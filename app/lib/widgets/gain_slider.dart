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
    // Chữ to (Cài đặt của máy): nhãn rộng thêm theo cỡ chữ (giới hạn để thanh còn chỗ), số phần
    // trăm co lại chứ không xuống dòng ("100" / "%").
    final scaler = MediaQuery.textScalerOf(context);
    return Row(
      children: [
        SizedBox(
          width: scaler.scale(120).clamp(120, 160).toDouble(),
          child: Text(label),
        ),
        Expanded(
          child: Slider(value: value, max: max, onChanged: onChanged),
        ),
        SizedBox(
          width: 48,
          child: FittedBox(
            fit: BoxFit.scaleDown,
            alignment: Alignment.centerRight,
            child: Text('${(value * 100).round()}%', maxLines: 1),
          ),
        ),
      ],
    );
  }
}
