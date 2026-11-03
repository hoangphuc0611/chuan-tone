import 'dart:io';

import 'package:flutter/services.dart';

import 'tonecore.dart' show InputKind;

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

/// (từ v0.9) Thiết bị micro trên Android: plugin Java tìm `AudioDeviceInfo` theo loại
/// (AudioManager.getDevices) — NDK không liệt kê được thiết bị.
abstract final class InputDevices {
  static const _channel = MethodChannel('tonecore_flutter/permission');

  /// Id thiết bị micro loại [kind] đang cắm; 0 nếu không có / [InputKind.auto] / không phải Android.
  static Future<int> idFor(InputKind kind) async {
    if (!Platform.isAndroid || kind == InputKind.auto) return 0;
    try {
      return await _channel.invokeMethod<int>('inputDeviceId', {
            'kind': kind.index,
          }) ??
          0;
    } on PlatformException {
      return 0;
    } on MissingPluginException {
      return 0;
    }
  }
}
