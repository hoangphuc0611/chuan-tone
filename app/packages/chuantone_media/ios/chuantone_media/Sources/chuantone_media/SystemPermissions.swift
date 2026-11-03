import AVFoundation
import Flutter
import UIKit

/// Kênh `chuantone/system` (M8 B5, hướng dẫn lần đầu): quyền micro — xem, xin (hộp thoại của iOS
/// hiện sau khi app giải thích vì sao cần) — và mở trang Cài đặt của app khi người dùng đã từ chối.
final class SystemPermissions {
  init(channel: FlutterMethodChannel) {
    channel.setMethodCallHandler { call, result in
      switch call.method {
      case "micPermission":
        result(Self.micStatus())
      case "requestMic":
        Self.requestMic { granted in DispatchQueue.main.async { result(granted) } }
      case "openAppSettings":
        if let url = URL(string: UIApplication.openSettingsURLString) {
          UIApplication.shared.open(url)
        }
        result(nil)
      default:
        result(FlutterMethodNotImplemented)
      }
    }
  }

  /// "granted" / "denied" / "undetermined".
  static func micStatus() -> String {
    if #available(iOS 17.0, *) {
      switch AVAudioApplication.shared.recordPermission {
      case .granted: return "granted"
      case .denied: return "denied"
      default: return "undetermined"
      }
    }
    switch AVAudioSession.sharedInstance().recordPermission {
    case .granted: return "granted"
    case .denied: return "denied"
    default: return "undetermined"
    }
  }

  static func requestMic(_ done: @escaping (Bool) -> Void) {
    if #available(iOS 17.0, *) {
      AVAudioApplication.requestRecordPermission(completionHandler: done)
    } else {
      AVAudioSession.sharedInstance().requestRecordPermission(done)
    }
  }
}
