import 'package:flutter/material.dart';

import 'app.dart';
import 'engine_info.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(ChuanToneApp(engineInfo: EngineInfo.probe()));
}
