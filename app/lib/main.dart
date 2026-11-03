import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app.dart';
import 'engine_info.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  registerNativeLicenses();
  runApp(ChuanToneApp(engineInfo: EngineInfo.probe()));
}

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
