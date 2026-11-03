import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app.dart';
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
/// bắt được. Không chặn lúc mở app.
void startTestLog() {
  unawaited(
    testLogSession().then((s) => TestLog.init(session: s), onError: (_) {}),
  );
  _lifecycle = AppLifecycleListener(
    onStateChange: (s) => TestLog.event('app', {'state': s.name}),
  );
  final previous = FlutterError.onError;
  FlutterError.onError = (d) {
    TestLog.error('Lỗi giao diện', details: d.exceptionAsString());
    previous?.call(d);
  };
  PlatformDispatcher.instance.onError = (e, _) {
    TestLog.error('Lỗi chưa bắt', details: '$e');
    return false; // để Flutter xử lý như thường
  };
}

// ignore: unused_element
AppLifecycleListener? _lifecycle;

/// Giấy phép thư viện native không do pub quản lý (pub tự đưa giấy phép gói Dart vào trang giấy
/// phép): Oboe (Apache-2.0) trong engine bản Android.
void registerNativeLicenses() {
  LicenseRegistry.addLicense(() async* {
    final apache = await rootBundle.loadString(
      'assets/licenses/apache-2.0.txt',
    );
    yield LicenseEntryWithLineBreaks(const [
      'Oboe',
    ], 'Copyright 2015 The Android Open Source Project\n\n$apache');
  });
}
