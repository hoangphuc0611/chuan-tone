import 'package:tonecore_flutter/tonecore_flutter.dart';

/// Kết quả kiểm tra engine lúc khởi động (M0: chứng minh app gọi được tc_engine_create).
class EngineInfo {
  const EngineInfo({
    required this.ok,
    this.version = '',
    this.apiVersion = '',
    this.framesProcessed = 0,
    this.error,
  });

  final bool ok;
  final String version;
  final String apiVersion;
  final int framesProcessed;
  final String? error;

  /// Tạo engine, xử lý thử một block im lặng rồi huỷ.
  static EngineInfo probe() {
    try {
      final engine = ToneCoreEngine.create();
      try {
        engine.start();
        engine.processOffline(null, 256);
        final frames = engine.metrics.framesProcessed;
        engine.stop();
        return EngineInfo(
          ok: ToneCore.isCompatible,
          version: ToneCore.version,
          apiVersion: ToneCore.apiVersionString,
          framesProcessed: frames,
          error: ToneCore.isCompatible ? null : 'Phiên bản API không khớp',
        );
      } finally {
        engine.dispose();
      }
    } catch (e) {
      return EngineInfo(ok: false, error: e.toString());
    }
  }
}
