import 'dart:async';

import 'package:flutter/foundation.dart';
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

  /// Tone đang chọn cho tự chỉnh giọng (mặc định La thứ – tone phổ biến của nhạc Việt).
  MusicKey key = const MusicKey(9, MusicScale.naturalMinor);

  /// Mức tự chỉnh giọng; null = tắt.
  CorrectionPreset? correction;

  ToneCoreEngine? get engine => _engine;
  bool get isRunning => _engine != null;

  /// Sự kiện từ engine (nạp beat xong, đổi route, đo độ trễ xong...).
  Stream<ToneCoreEvent> get events => _events.stream;

  /// Xin quyền micro và mở thiết bị âm thanh. Trả về true nếu chạy được.
  Future<bool> ensureStarted() async {
    if (_engine != null) return true;
    micGranted = await MicPermission.request();
    if (!micGranted) {
      lastError = 'Chưa cấp quyền micro. Vào Cài đặt của máy để bật quyền micro cho ChuẩnTone.';
      notifyListeners();
      return false;
    }
    try {
      final e = ToneCoreEngine.platform();
      e.start();
      e.key = key;
      e.applyCorrectionPreset(correction ?? CorrectionPreset.singer);
      e.pitchCorrectionEnabled = correction != null;
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

  void setKey(MusicKey k) {
    key = k;
    _engine?.key = k;
    notifyListeners();
  }

  /// null = tắt tự chỉnh giọng.
  void setCorrection(CorrectionPreset? p) {
    correction = p;
    final e = _engine;
    if (e != null) {
      if (p != null) e.applyCorrectionPreset(p);
      e.pitchCorrectionEnabled = p != null;
    }
    notifyListeners();
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
