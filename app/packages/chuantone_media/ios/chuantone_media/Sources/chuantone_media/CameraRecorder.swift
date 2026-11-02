import AVFoundation
import Flutter
import UIKit

/// Camera trước cho video 9:16 khi hát (M7 B6, ADR-007 điểm 5).
///
/// - AVCaptureSession chỉ có hình (không thêm micro) và `automaticallyConfiguresApplicationAudioSession
///   = false` → không đụng AVAudioSession của engine (RemoteIO vẫn chạy như cũ).
/// - Khung hình dọc 1080×1920, lật gương như lúc xem trước. Mốc thời gian của khung hình theo đồng hồ
///   của phiên (đồng hồ máy — cùng gốc với `mach_absolute_time` mà engine dùng cho tiếng): app ghép
///   hình vào tiếng theo độ lệch (mốc khung đầu) − (lúc beat frame đầu được nghe).
/// - Ghi H.264 bằng AVAssetWriter, phiên bắt đầu ở mốc khung đầu tiên.
final class CameraRecorder: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
  static let shared = CameraRecorder()

  let session = AVCaptureSession()
  private let queue = DispatchQueue(label: "chuantone.camera")
  private let output = AVCaptureVideoDataOutput()
  private var configured = false

  // Ghi (chỉ dùng trên `queue`).
  private var writer: AVAssetWriter?
  private var input: AVAssetWriterInput?
  private var wantStart = false
  private var firstPts: CMTime?
  private var lastPts: CMTime?
  private var frames = 0
  private var dropped = 0

  struct Clip {
    let path: String
    let firstFrameNs: Int64
    let durationSec: Double
    let frames: Int
    let dropped: Int
  }

  /// Xin quyền + dựng phiên (một lần) + chạy xem trước.
  func start(completion: @escaping (Error?) -> Void) {
    AVCaptureDevice.requestAccess(for: .video) { granted in
      guard granted else {
        completion(MediaError.failed("chưa được phép dùng camera"))
        return
      }
      self.queue.async {
        do {
          try self.configureIfNeeded()
          if !self.session.isRunning { self.session.startRunning() }
          completion(nil)
        } catch {
          completion(error)
        }
      }
    }
  }

  func stop() {
    queue.async {
      if self.session.isRunning { self.session.stopRunning() }
    }
  }

  private func configureIfNeeded() throws {
    if configured { return }
    session.beginConfiguration()
    defer { session.commitConfiguration() }
    session.automaticallyConfiguresApplicationAudioSession = false
    session.sessionPreset = .hd1920x1080
    guard
      let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .front)
    else { throw MediaError.failed("máy không có camera trước") }
    let camInput = try AVCaptureDeviceInput(device: device)
    guard session.canAddInput(camInput) else { throw MediaError.failed("không mở được camera") }
    session.addInput(camInput)
    output.videoSettings = [
      kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
    ]
    output.alwaysDiscardsLateVideoFrames = false
    output.setSampleBufferDelegate(self, queue: queue)
    guard session.canAddOutput(output) else { throw MediaError.failed("không lấy được hình") }
    session.addOutput(output)
    if let conn = output.connection(with: .video) {
      if #available(iOS 17.0, *) {
        if conn.isVideoRotationAngleSupported(90) { conn.videoRotationAngle = 90 }
      } else if conn.isVideoOrientationSupported {
        conn.videoOrientation = .portrait
      }
      if conn.isVideoMirroringSupported {
        conn.automaticallyAdjustsVideoMirroring = false
        conn.isVideoMirrored = true  // như gương, giống lúc xem trước
      }
    }
    // 30 khung / giây cố định cho video đều.
    try device.lockForConfiguration()
    device.activeVideoMinFrameDuration = CMTime(value: 1, timescale: 30)
    device.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: 30)
    device.unlockForConfiguration()
    configured = true
  }

  /// Bắt đầu ghi ở khung hình kế tiếp.
  func startRecording(to url: URL, completion: @escaping (Error?) -> Void) {
    queue.async {
      do {
        try? FileManager.default.removeItem(at: url)
        let w = try AVAssetWriter(outputURL: url, fileType: .mp4)
        let i = AVAssetWriterInput(
          mediaType: .video,
          outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: 1080,
            AVVideoHeightKey: 1920,
            AVVideoCompressionPropertiesKey: [
              AVVideoAverageBitRateKey: 8_000_000,
              AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
              AVVideoMaxKeyFrameIntervalKey: 60,
            ],
          ])
        i.expectsMediaDataInRealTime = true
        guard w.canAdd(i) else { throw MediaError.failed("không ghi được hình") }
        w.add(i)
        self.writer = w
        self.input = i
        self.firstPts = nil
        self.lastPts = nil
        self.frames = 0
        self.dropped = 0
        self.wantStart = true
        completion(nil)
      } catch {
        completion(error)
      }
    }
  }

  /// Dừng ghi; trả đoạn hình đã ghi (mốc khung đầu theo đồng hồ máy, ns).
  func stopRecording(completion: @escaping (Result<Clip, Error>) -> Void) {
    queue.async {
      self.wantStart = false
      guard let w = self.writer, let i = self.input else {
        completion(.failure(MediaError.failed("chưa ghi hình")))
        return
      }
      self.writer = nil
      self.input = nil
      guard let first = self.firstPts, w.status == .writing else {
        w.cancelWriting()
        completion(.failure(MediaError.failed("chưa có khung hình nào")))
        return
      }
      let last = self.lastPts ?? first
      let frames = self.frames
      let dropped = self.dropped
      i.markAsFinished()
      w.finishWriting {
        if w.status == .completed {
          completion(
            .success(
              Clip(
                path: w.outputURL.path,
                firstFrameNs: Int64((CMTimeGetSeconds(first) * 1e9).rounded()),
                durationSec: CMTimeGetSeconds(CMTimeSubtract(last, first)) + 1.0 / 30,
                frames: frames, dropped: dropped)))
        } else {
          completion(.failure(MediaError.failed(w.error?.localizedDescription ?? "ghi hình lỗi")))
        }
      }
    }
  }

  // MARK: - AVCaptureVideoDataOutputSampleBufferDelegate (trên `queue`)

  func captureOutput(
    _ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
    from connection: AVCaptureConnection
  ) {
    guard let w = writer, let i = input else { return }
    let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
    if wantStart {
      guard w.startWriting() else { return }
      w.startSession(atSourceTime: pts)
      firstPts = pts
      wantStart = false
    }
    guard w.status == .writing else { return }
    if i.isReadyForMoreMediaData, i.append(sampleBuffer) {
      frames += 1
      lastPts = pts
    } else {
      dropped += 1
    }
  }

  func captureOutput(
    _ output: AVCaptureOutput, didDrop sampleBuffer: CMSampleBuffer,
    from connection: AVCaptureConnection
  ) {
    if writer != nil && firstPts != nil { dropped += 1 }
  }
}

/// Khung xem trước camera cho Flutter (platform view `chuantone/camera-preview`).
final class CameraPreviewFactory: NSObject, FlutterPlatformViewFactory {
  func create(withFrame frame: CGRect, viewIdentifier viewId: Int64, arguments args: Any?)
    -> FlutterPlatformView
  {
    CameraPreview(frame: frame)
  }
}

final class CameraPreview: NSObject, FlutterPlatformView {
  private let container: PreviewView

  init(frame: CGRect) {
    container = PreviewView(frame: frame)
    super.init()
  }

  func view() -> UIView { container }
}

final class PreviewView: UIView {
  override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }

  override init(frame: CGRect) {
    super.init(frame: frame)
    let layer = self.layer as! AVCaptureVideoPreviewLayer
    layer.session = CameraRecorder.shared.session
    layer.videoGravity = .resizeAspectFill
    backgroundColor = .black
  }

  required init?(coder: NSCoder) { fatalError("không dùng") }
}
