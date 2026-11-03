import 'dart:async';
import 'dart:io';

import 'package:flutter/services.dart';

import 'tonecore.dart';

/// Theo dõi loại thiết bị âm thanh trên Android và báo cho engine; cắm / rút thiết bị thì chọn lại
/// micro đã chọn (v0.9).
///
/// iOS không cần: engine tự đọc route từ AVAudioSession.
class RouteMonitor {
  RouteMonitor(this._engine);

  static const _channel = EventChannel('tonecore_flutter/route');

  final ToneCoreEngine _engine;
  StreamSubscription<dynamic>? _sub;

  void start() {
    if (!Platform.isAndroid || _sub != null) return;
    _sub = _channel.receiveBroadcastStream().listen((value) {
      if (value is int && value >= 0 && value < AudioRoute.values.length) {
        _engine.setRouteHint(AudioRoute.values[value]);
        // Cắm / rút thiết bị: tìm lại id của micro đã chọn (soundcard cắm vào sau vẫn được chọn).
        final wanted = _engine.inputWanted;
        if (wanted != InputKind.auto) unawaited(_engine.selectInput(wanted));
      }
    });
  }

  void stop() {
    _sub?.cancel();
    _sub = null;
  }
}
