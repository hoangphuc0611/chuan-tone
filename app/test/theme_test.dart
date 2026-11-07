// Bảng màu giao diện mới (docs/ui/redesign-plan.md): chữ đủ tương phản (WCAG AA 4,5:1), chữ phụ không
// nhỏ hơn 14, nút Thu đỏ giống nhau ở cả hai giao diện.

import 'package:chuantone/theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

double contrast(Color a, Color b) {
  final x = a.computeLuminance();
  final y = b.computeLuminance();
  return (x > y ? x + 0.05 : y + 0.05) / (x > y ? y + 0.05 : x + 0.05);
}

void main() {
  for (final b in Brightness.values) {
    final cs = appColors(b);
    test('tương phản chữ — ${b.name}', () {
      final pairs = <String, (Color, Color)>{
        'chữ / nền': (cs.onSurface, cs.surface),
        'chữ phụ / nền': (cs.onSurfaceVariant, cs.surface),
        'chữ phụ / thẻ': (cs.onSurfaceVariant, cs.surfaceContainerLowest),
        'chữ phụ / nền xám': (cs.onSurfaceVariant, cs.surfaceContainerHigh),
        'nút chính': (cs.onPrimary, cs.primary),
        'chip đang chọn': (cs.onPrimary, cs.primary),
        'tint đỏ': (cs.onPrimaryContainer, cs.primaryContainer),
        'tint vàng': (cs.onSecondaryContainer, cs.secondaryContainer),
        'tint xanh': (cs.onTertiaryContainer, cs.tertiaryContainer),
        'lỗi': (cs.onErrorContainer, cs.errorContainer),
      };
      pairs.forEach((name, p) {
        expect(contrast(p.$1, p.$2), greaterThanOrEqualTo(4.5), reason: name);
      });
    });

    test('chữ phụ tối thiểu 14 — ${b.name}', () {
      final t = appTheme(b).textTheme;
      for (final s in [t.bodySmall, t.labelSmall, t.labelMedium]) {
        expect(s!.fontSize, greaterThanOrEqualTo(14));
      }
      expect(t.bodyMedium!.fontFamily, 'BeVietnamPro');
    });
  }

  test('nút Thu, chữ trắng trên phần đỏ của thẻ lớn', () {
    expect(contrast(Colors.white, recordRed), greaterThanOrEqualTo(4.5));
    expect(
      contrast(Colors.white, heroGradient.colors.first),
      greaterThanOrEqualTo(4.5),
    );
  });
}
