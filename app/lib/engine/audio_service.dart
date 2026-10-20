import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

/// Giữ một engine ToneCore dùng chung cho cả app (thiết bị âm thanh thật).
///
/// Mở engine khi màn đầu tiên cần, xin quyền micro trước khi mở.
class AudioService extends ChangeNotifier {
  AudioService._();

  static final AudioService instance = AudioService._();

  ToneCoreEngine? _engine;
  RouteMonitor? _routeMonitor;
  StreamSubscription<ToneCoreEvent>? _eventSub;
  final _events = StreamController<ToneCoreEvent>.broadcast();

  String? lastError;
  bool micGranted = false;

  ToneCoreEngine? get engine => _engine;
  bool get isRunning => _engine != null;

  /// Sự kiện từ engine (nạp beat xong, đổi route, đo độ trễ xong...).
  Stream<ToneCoreEvent> get events => _events.stream;

  /// Xin quyền micro và mở thiết bị âm thanh. Trả về true nếu chạy được.
  Future<bool> ensureStarted() async {
    if (_engine != null) return true;
    final status = await Permission.microphone.request();
    micGranted = status.isGranted;
    if (!micGranted) {
      lastError = 'Chưa cấp quyền micro. Vào Cài đặt của máy để bật quyền micro cho ChuẩnTone.';
      notifyListeners();
      return false;
    }
    try {
      final e = ToneCoreEngine.platform();
      e.start();
      _engine = e;
      _routeMonitor = RouteMonitor(e)..start();
      _eventSub = e.events.listen(_events.add);
      lastError = null;
    } on ToneCoreException catch (err) {
      lastError = 'Không mở được thiết bị âm thanh (${err.codeName}).';
    }
    notifyListeners();
    return _engine != null;
  }

  Future<void> shutdown() async {
    await _eventSub?.cancel();
    _eventSub = null;
    _routeMonitor?.stop();
    _routeMonitor = null;
    _engine?.dispose();
    _engine = null;
    notifyListeners();
  }
}
