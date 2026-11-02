import Flutter
import UIKit

/// Kênh `chuantone/media` (M7, docs/m7-plan.md): API native của iOS cho phần xuất / chia sẻ.
public class ChuanToneMediaPlugin: NSObject, FlutterPlugin {
  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "chuantone/media", binaryMessenger: registrar.messenger())
    registrar.addMethodCallDelegate(ChuanToneMediaPlugin(), channel: channel)
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let args = call.arguments as? [String: Any] ?? [:]
    switch call.method {
    case "share":
      guard let path = args["path"] as? String else {
        result(FlutterError(code: "args", message: "thiếu path", details: nil))
        return
      }
      share(URL(fileURLWithPath: path), result: result)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  /// Bảng chia sẻ của hệ điều hành (TikTok, Facebook, Zalo… đã cài sẽ tự hiện; "Lưu vào Tệp").
  /// Trả true nếu người dùng đã chọn một đích, false nếu đóng bảng.
  private func share(_ url: URL, result: @escaping FlutterResult) {
    guard let top = Self.topViewController() else {
      result(FlutterError(code: "ui", message: "không tìm thấy màn hình", details: nil))
      return
    }
    let vc = UIActivityViewController(activityItems: [url], applicationActivities: nil)
    vc.completionWithItemsHandler = { _, completed, _, error in
      if let error = error {
        result(FlutterError(code: "share", message: error.localizedDescription, details: nil))
      } else {
        result(completed)
      }
    }
    if let pop = vc.popoverPresentationController {  // iPad: bảng chia sẻ là popover
      pop.sourceView = top.view
      pop.sourceRect = CGRect(x: top.view.bounds.midX, y: top.view.bounds.midY, width: 1, height: 1)
      pop.permittedArrowDirections = []
    }
    top.present(vc, animated: true)
  }

  static func topViewController() -> UIViewController? {
    let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
    let window = scenes.flatMap { $0.windows }.first { $0.isKeyWindow } ?? scenes.first?.windows.first
    var top = window?.rootViewController
    while let presented = top?.presentedViewController { top = presented }
    return top
  }
}
