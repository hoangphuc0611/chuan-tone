import 'dart:io';

import 'package:flutter/services.dart';

/// Quyền micro.
///
/// - Android: hỏi qua plugin Java (RECORD_AUDIO).
/// - iOS: engine tự hỏi khi mở micro lần đầu (AVAudioSession.requestRecordPermission);
///   hàm này trả true để app mở engine, hộp thoại của iOS sẽ hiện lúc đó.
abstract final class MicPermission {
  static const _channel = MethodChannel('tonecore_flutter/permission');

  static Future<bool> request() async {
    if (!Platform.isAndroid) return true;
    final granted = await _channel.invokeMethod<bool>('requestMicrophone');
    return granted ?? false;
  }
}
