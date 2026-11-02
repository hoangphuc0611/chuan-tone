import AVFoundation
import Flutter
import Photos
import UIKit

/// Kênh `chuantone/media` (M7, docs/m7-plan.md): API native của iOS cho phần xuất / chia sẻ.
/// Việc nặng chạy ở hàng đợi nền; kết quả trả về luồng chính. Một việc dựng video tại một thời
/// điểm (`progress` / `cancel` áp cho việc đó).
public class ChuanToneMediaPlugin: NSObject, FlutterPlugin {
  private var waveform: WaveformVideo?

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "chuantone/media", binaryMessenger: registrar.messenger())
    registrar.addMethodCallDelegate(ChuanToneMediaPlugin(), channel: channel)
    registrar.register(CameraPreviewFactory(), withId: "chuantone/camera-preview")
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let args = call.arguments as? [String: Any] ?? [:]
    switch call.method {
    case "share":
      guard let path = args["path"] as? String else { return result(Self.argError("path")) }
      share(URL(fileURLWithPath: path), result: result)
    case "renderWaveformVideo":
      guard let audio = args["audio"] as? String, let output = args["output"] as? String else {
        return result(Self.argError("audio / output"))
      }
      let argb = (args["color"] as? NSNumber)?.uint32Value ?? 0xFFE5_484D
      let style = WaveformVideo.Style(
        title: args["title"] as? String ?? "",
        subtitle: args["subtitle"] as? String ?? "",
        color: UIColor(
          red: CGFloat((argb >> 16) & 0xFF) / 255, green: CGFloat((argb >> 8) & 0xFF) / 255,
          blue: CGFloat(argb & 0xFF) / 255, alpha: 1))
      let job = WaveformVideo()
      waveform = job
      DispatchQueue.global(qos: .userInitiated).async {
        do {
          try job.render(
            audio: URL(fileURLWithPath: audio), output: URL(fileURLWithPath: output), style: style)
          DispatchQueue.main.async { result(true) }
        } catch let e as MediaError {
          DispatchQueue.main.async {
            if case .cancelled = e { result(false) } else {
              result(FlutterError(code: "video", message: e.message, details: nil))
            }
          }
        } catch {
          DispatchQueue.main.async {
            result(FlutterError(code: "video", message: error.localizedDescription, details: nil))
          }
        }
      }
    case "progress":
      result(waveform?.progress ?? 0)
    case "cancel":
      waveform?.cancel()
      result(nil)
    case "cameraStart":
      CameraRecorder.shared.start { error in
        DispatchQueue.main.async {
          if let error = error {
            result(FlutterError(code: "camera", message: Self.message(error), details: nil))
          } else {
            result(true)
          }
        }
      }
    case "cameraStop":
      CameraRecorder.shared.stop()
      result(nil)
    case "cameraRecordStart":
      guard let path = args["path"] as? String else { return result(Self.argError("path")) }
      CameraRecorder.shared.startRecording(to: URL(fileURLWithPath: path)) { error in
        DispatchQueue.main.async {
          if let error = error {
            result(FlutterError(code: "camera", message: Self.message(error), details: nil))
          } else {
            result(true)
          }
        }
      }
    case "cameraRecordStop":
      CameraRecorder.shared.stopRecording { r in
        DispatchQueue.main.async {
          switch r {
          case .success(let clip):
            result([
              "path": clip.path, "firstFrameNs": NSNumber(value: clip.firstFrameNs),
              "durationSec": clip.durationSec, "frames": clip.frames, "dropped": clip.dropped,
            ])
          case .failure(let error):
            result(FlutterError(code: "camera", message: Self.message(error), details: nil))
          }
        }
      }
    case "muxVideo":
      guard let video = args["video"] as? String, let audio = args["audio"] as? String,
        let output = args["output"] as? String
      else { return result(Self.argError("video / audio / output")) }
      let offsetNs = (args["offsetNs"] as? NSNumber)?.int64Value ?? 0
      DispatchQueue.global(qos: .userInitiated).async {
        do {
          try MediaMux.mux(
            video: URL(fileURLWithPath: video),
            videoOffset: CMTime(value: offsetNs, timescale: 1_000_000_000),
            audio: URL(fileURLWithPath: audio), trim: nil, output: URL(fileURLWithPath: output))
          DispatchQueue.main.async { result(true) }
        } catch {
          DispatchQueue.main.async {
            result(FlutterError(code: "video", message: Self.message(error), details: nil))
          }
        }
      }
    case "saveVideoToPhotos":
      guard let path = args["path"] as? String else { return result(Self.argError("path")) }
      saveVideo(URL(fileURLWithPath: path), result: result)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  private static func message(_ error: Error) -> String {
    (error as? MediaError)?.message ?? error.localizedDescription
  }

  private static func argError(_ what: String) -> FlutterError {
    FlutterError(code: "args", message: "thiếu \(what)", details: nil)
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

  /// Lưu video vào Ảnh (quyền "chỉ thêm"). true = đã lưu; false = người dùng không cho phép.
  private func saveVideo(_ url: URL, result: @escaping FlutterResult) {
    PHPhotoLibrary.requestAuthorization(for: .addOnly) { status in
      guard status == .authorized || status == .limited else {
        DispatchQueue.main.async { result(false) }
        return
      }
      PHPhotoLibrary.shared().performChanges({
        PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: url)
      }) { ok, error in
        DispatchQueue.main.async {
          if let error = error {
            result(FlutterError(code: "photos", message: error.localizedDescription, details: nil))
          } else {
            result(ok)
          }
        }
      }
    }
  }

  static func topViewController() -> UIViewController? {
    let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
    let window = scenes.flatMap { $0.windows }.first { $0.isKeyWindow } ?? scenes.first?.windows.first
    var top = window?.rootViewController
    while let presented = top?.presentedViewController { top = presented }
    return top
  }
}
