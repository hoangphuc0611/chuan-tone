import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

/// Màu chủ đạo của ChuẩnTone (giao diện Stitch, docs/ui/redesign-plan.md mục 2).
const brandColor = Color(0xFFB1212D);

/// Nút Thu: luôn đỏ đặc, ở mọi giao diện.
const recordRed = Color(0xFFD32F2F);

/// Đúng nốt / ổn.
const goodGreen = Color(0xFF00854A);

/// Hơi lệch / cần chú ý.
const warnOrange = Color(0xFFB85C00);

/// Dải màu thẻ lớn ("ánh đèn sân khấu"). Chữ trắng chỉ đặt ở phần đỏ: đầu vàng tương phản thấp.
const heroGradient = LinearGradient(
  colors: [Color(0xFFB1212D), Color(0xFFD43C42), Color(0xFFFFB548)],
  stops: [0, 0.55, 1],
);

/// Dải đỏ cho thẻ nhiều chữ (chữ trắng đọc rõ trên cả thẻ).
const redGradient = LinearGradient(
  colors: [Color(0xFFB1212D), Color(0xFFC62F3A)],
);

/// Nền sân khấu của Màn hát: tím đêm, sáng nhẹ từ trên xuống.
const stageGradient = LinearGradient(
  begin: Alignment.topCenter,
  end: Alignment.bottomCenter,
  colors: [Color(0xFF2A1630), Color(0xFF15111A)],
);

/// Bo góc thẻ lớn.
const cardRadius = 24.0;

const _light = ColorScheme(
  brightness: Brightness.light,
  primary: brandColor,
  onPrimary: Colors.white,
  primaryContainer: Color(0xFFFFDAD8),
  onPrimaryContainer: Color(0xFF92011A),
  secondary: Color(0xFF8A5A00),
  onSecondary: Colors.white,
  secondaryContainer: Color(0xFFFFDDB4),
  onSecondaryContainer: Color(0xFF5C3B00),
  tertiary: Color(0xFF006A3A),
  onTertiary: Colors.white,
  tertiaryContainer: Color(0xFF7EFBAB),
  onTertiaryContainer: Color(0xFF00391D),
  error: Color(0xFFBA1A1A),
  onError: Colors.white,
  errorContainer: Color(0xFFFFDAD6),
  onErrorContainer: Color(0xFF93000A),
  surface: Color(0xFFFFF7FF),
  onSurface: Color(0xFF1E1A22),
  onSurfaceVariant: Color(0xFF5A403F),
  surfaceContainerLowest: Colors.white,
  surfaceContainerLow: Color(0xFFFAF1FB),
  surfaceContainer: Color(0xFFF4EBF7),
  surfaceContainerHigh: Color(0xFFEEE5F1),
  surfaceContainerHighest: Color(0xFFE8DFEB),
  outline: Color(0xFF8E7071),
  outlineVariant: Color(0xFFE2D3DA),
);

const _dark = ColorScheme(
  brightness: Brightness.dark,
  primary: Color(0xFFFF6B73),
  onPrimary: Color(0xFF3A0409),
  primaryContainer: Color(0xFF5A1A24),
  onPrimaryContainer: Color(0xFFFFDAD8),
  secondary: Color(0xFFFFB548),
  onSecondary: Color(0xFF442B00),
  secondaryContainer: Color(0xFF5C3F0B),
  onSecondaryContainer: Color(0xFFFFDDB4),
  tertiary: Color(0xFF5FE3A0),
  onTertiary: Color(0xFF00391D),
  tertiaryContainer: Color(0xFF00522C),
  onTertiaryContainer: Color(0xFF7EFBAB),
  error: Color(0xFFFFB4AB),
  onError: Color(0xFF690005),
  errorContainer: Color(0xFF93000A),
  onErrorContainer: Color(0xFFFFDAD6),
  surface: Color(0xFF15111A),
  onSurface: Color(0xFFEDE4F0),
  onSurfaceVariant: Color(0xFFCFBFC8),
  surfaceContainerLowest: Color(0xFF100C14),
  surfaceContainerLow: Color(0xFF1C1622),
  surfaceContainer: Color(0xFF211B26),
  surfaceContainerHigh: Color(0xFF2C2532),
  surfaceContainerHighest: Color(0xFF37303D),
  outline: Color(0xFF9A8A93),
  outlineVariant: Color(0xFF4A3F4C),
);

/// Bảng màu theo sáng / tối.
ColorScheme appColors(Brightness brightness) =>
    brightness == Brightness.light ? _light : _dark;

/// Giao diện sáng / tối theo cài đặt của máy (Màn hát luôn tối, kiểu sân khấu). Chữ phụ tối thiểu 14:
/// người hát 18–50 tuổi, nhiều người không đọc được chữ nhỏ.
ThemeData appTheme(Brightness brightness) {
  final cs = appColors(brightness);
  final base = ThemeData(
    colorScheme: cs,
    useMaterial3: true,
    fontFamily: 'BeVietnamPro',
    scaffoldBackgroundColor: cs.surface,
  );
  final t = base.textTheme;
  final text = t.copyWith(
    displaySmall: t.displaySmall?.copyWith(
      fontWeight: FontWeight.w700,
      letterSpacing: -0.5,
    ),
    headlineSmall: t.headlineSmall?.copyWith(fontWeight: FontWeight.w700),
    titleLarge: t.titleLarge?.copyWith(
      fontSize: 20,
      fontWeight: FontWeight.w600,
    ),
    titleMedium: t.titleMedium?.copyWith(fontWeight: FontWeight.w600),
    titleSmall: t.titleSmall?.copyWith(fontWeight: FontWeight.w600),
    bodySmall: t.bodySmall?.copyWith(fontSize: 14, letterSpacing: 0),
    labelLarge: t.labelLarge?.copyWith(
      fontSize: 15,
      fontWeight: FontWeight.w600,
    ),
    labelMedium: t.labelMedium?.copyWith(
      fontSize: 14,
      fontWeight: FontWeight.w600,
    ),
    labelSmall: t.labelSmall?.copyWith(
      fontSize: 14,
      fontWeight: FontWeight.w600,
    ),
  );
  return base.copyWith(
    textTheme: text,
    appBarTheme: AppBarTheme(
      backgroundColor: cs.surface,
      foregroundColor: cs.onSurface,
      surfaceTintColor: Colors.transparent,
      scrolledUnderElevation: 0,
      titleTextStyle: text.titleLarge?.copyWith(color: cs.onSurface),
    ),
    cardTheme: CardThemeData(
      color: cs.surfaceContainerLowest,
      surfaceTintColor: Colors.transparent,
      elevation: 1,
      shadowColor: const Color(0x22B1212D),
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(cardRadius),
      ),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        minimumSize: const Size(0, 52),
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
        shape: const StadiumBorder(),
        textStyle: text.labelLarge,
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        minimumSize: const Size(0, 48),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 10),
        shape: const StadiumBorder(),
        side: BorderSide(color: cs.outline),
        textStyle: text.labelLarge,
      ),
    ),
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(textStyle: text.labelLarge),
    ),
    chipTheme: ChipThemeData(
      shape: const StadiumBorder(),
      side: BorderSide(color: cs.outlineVariant),
      backgroundColor: cs.surfaceContainerLowest,
      selectedColor: cs.primary,
      showCheckmark: false,
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 6),
      labelPadding: const EdgeInsets.symmetric(horizontal: 4),
    ),
    segmentedButtonTheme: SegmentedButtonThemeData(
      style: SegmentedButton.styleFrom(
        selectedBackgroundColor: cs.primary,
        selectedForegroundColor: cs.onPrimary,
      ),
    ),
    sliderTheme: SliderThemeData(
      trackHeight: 6,
      activeTrackColor: cs.primary,
      inactiveTrackColor: cs.surfaceContainerHighest,
      thumbColor: cs.primary,
      overlayColor: cs.primary.withValues(alpha: 0.12),
    ),
    dialogTheme: DialogThemeData(
      backgroundColor: cs.surfaceContainerLowest,
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(cardRadius + 4),
      ),
    ),
    bottomSheetTheme: BottomSheetThemeData(
      backgroundColor: cs.surfaceContainerLowest,
      surfaceTintColor: Colors.transparent,
      showDragHandle: true,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(
          top: Radius.circular(cardRadius + 4),
        ),
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
    ),
    navigationBarTheme: NavigationBarThemeData(
      backgroundColor: cs.surfaceContainerLowest,
      surfaceTintColor: Colors.transparent,
      indicatorColor: cs.primaryContainer,
      labelTextStyle: WidgetStatePropertyAll(text.labelSmall),
      iconTheme: WidgetStateProperty.resolveWith(
        (states) => IconThemeData(
          color: states.contains(WidgetState.selected)
              ? cs.onPrimaryContainer
              : cs.onSurfaceVariant,
        ),
      ),
    ),
  );
}

/// Chỉ tiếng Việt (M8): chữ có sẵn của Flutter — Quay lại, Huỷ, Sao chép, trang giấy phép… — theo
/// `flutter_localizations`.
const appLocale = Locale('vi');
const appLocalizationsDelegates = <LocalizationsDelegate<dynamic>>[
  GlobalMaterialLocalizations.delegate,
  GlobalWidgetsLocalizations.delegate,
  GlobalCupertinoLocalizations.delegate,
];
