import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app.dart';
import 'data/app_health.dart';
import 'data/test_log.dart';
import 'engine_info.dart';
import 'pages/settings_page.dart' show testLogSession;

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  registerNativeLicenses();
  startTestLog();
  runApp(ChuanToneApp(engineInfo: EngineInfo.probe()));
}

/// Nhật ký thử nghiệm (M8 B7, bật ở Cài đặt → Nâng cao): mốc phiên, app vào nền / quay lại, lỗi chưa
/// bắt được. Cùng chỗ: đếm phiên + nhật ký lỗi luôn bật (M9 B5, [AppHealth]). Không chặn lúc mở app.
void startTestLog() {
  unawaited(
    testLogSession().then((s) => TestLog.init(session: s), onError: (_) {}),
  );
  // M9 B5: đếm phiên / thoát bất thường, nhật ký lỗi — luôn bật, chỉ nằm trên máy.
  unawaited(AppHealth.init().catchError((_) {}));
  _lifecycle = AppLifecycleListener(
    onStateChange: (s) {
      TestLog.event('app', {'state': s.name});
      switch (s) {
        case AppLifecycleState.resumed:
          unawaited(AppHealth.foreground());
        case AppLifecycleState.paused || AppLifecycleState.detached:
          unawaited(AppHealth.background());
        case AppLifecycleState.inactive || AppLifecycleState.hidden:
          break; // vẫn đang chuyển (vd kéo trung tâm điều khiển), chưa tính là vào nền
      }
    },
  );
  final previous = FlutterError.onError;
  FlutterError.onError = (d) {
    TestLog.error('Lỗi giao diện', details: d.exceptionAsString());
    AppHealth.recordError(
      'Lỗi giao diện',
      '${d.exceptionAsString()}\n${d.stack ?? ''}',
    );
    previous?.call(d);
  };
  PlatformDispatcher.instance.onError = (e, st) {
    TestLog.error('Lỗi chưa bắt', details: '$e');
    AppHealth.recordError('Lỗi chưa bắt', '$e\n$st');
    return false; // để Flutter xử lý như thường
  };
}

// ignore: unused_element
AppLifecycleListener? _lifecycle;

/// Giấy phép không do pub quản lý (pub tự đưa giấy phép gói Dart vào trang giấy phép): Oboe
/// (Apache-2.0) trong engine bản Android, font Be Vietnam Pro (SIL OFL 1.1).
void registerNativeLicenses() {
  LicenseRegistry.addLicense(() async* {
    final apache = await rootBundle.loadString(
      'assets/licenses/apache-2.0.txt',
    );
    yield LicenseEntryWithLineBreaks(const [
      'Oboe',
    ], 'Copyright 2015 The Android Open Source Project\n\n$apache');
    final ofl = await rootBundle.loadString(
      'assets/licenses/ofl-1.1-be-vietnam-pro.txt',
    );
    yield LicenseEntryWithLineBreaks(const ['Be Vietnam Pro'], ofl);
  });
}
