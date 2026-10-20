import 'dart:async';
import 'dart:io';

import 'package:flutter/services.dart';

import 'tonecore.dart';

/// Theo dõi loại thiết bị âm thanh trên Android và báo cho engine.
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
      }
    });
  }

  void stop() {
    _sub?.cancel();
    _sub = null;
  }
}
