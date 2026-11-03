import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

/// Màu chủ đạo của ChuẩnTone.
const brandColor = Color(0xFFE5484D);

/// Giao diện sáng / tối (M8 B5: tối theo cài đặt của máy) cùng một màu chủ đạo.
ThemeData appTheme(Brightness brightness) => ThemeData(
  colorScheme: ColorScheme.fromSeed(
    seedColor: brandColor,
    brightness: brightness,
  ),
  useMaterial3: true,
);

/// Chỉ tiếng Việt (M8): chữ có sẵn của Flutter — Quay lại, Huỷ, Sao chép, trang giấy phép… — theo
/// `flutter_localizations`.
const appLocale = Locale('vi');
const appLocalizationsDelegates = <LocalizationsDelegate<dynamic>>[
  GlobalMaterialLocalizations.delegate,
  GlobalWidgetsLocalizations.delegate,
  GlobalCupertinoLocalizations.delegate,
];
