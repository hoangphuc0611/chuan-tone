import AVFoundation
import Accelerate
import CoreGraphics
import CoreText
import UIKit

/// Dựng video 9:16 nền sóng nhạc + tên bài từ file âm thanh (M7 B5, docs/m7-plan.md mục 3):
///  1. Đọc âm thanh (AVAudioFile) → mỗi khung hình 1/30 s: phổ 2048 điểm (vDSP) gom 40 cột theo
///     thang log 60 Hz – 12 kHz, làm mượt lên nhanh / xuống chậm.
///  2. Vẽ từng khung (CoreGraphics trên CVPixelBuffer): nền + tên bài vẽ sẵn một lần, mỗi khung vẽ
///     cột sóng nhạc (đối xứng), thanh thời gian → AVAssetWriter H.264 1080×1920, 30 fps, không tiếng.
///  3. Ghép hình + chính file âm thanh (AVMutableComposition, xuất nguyên dạng — không nén lại) →
///     MP4. Hình dựng từ đúng file tiếng nên khớp tiếng tuyệt đối.
final class WaveformVideo {
  static let width = 1080
  static let height = 1920
  static let fps: Int32 = 30
  private static let bars = 40
  private static let fftSize = 2048

  struct Style {
    var title: String
    var subtitle: String
    var color: UIColor
  }

  /// 0..1, đọc từ luồng khác (atomic đủ dùng cho hiển thị).
  private(set) var progress: Double = 0
  private var cancelled = false

  func cancel() { cancelled = true }

  func render(audio: URL, output: URL, style: Style) throws {
    let file = try AVAudioFile(forReading: audio)
    let rate = file.processingFormat.sampleRate
    let totalFrames = AVAudioFramePosition(file.length)
    let frameCount = Int((Double(totalFrames) / rate * Double(Self.fps)).rounded(.up))
    guard frameCount > 0 else { throw MediaError.badInput("file âm thanh rỗng") }
    let levels = try Self.spectrum(file: file, frames: frameCount, rate: rate) { p in
      self.progress = 0.25 * p
      return !self.cancelled
    }
    if cancelled { throw MediaError.cancelled }

    let silent = output.deletingLastPathComponent()
      .appendingPathComponent(".hinh-\(UUID().uuidString).mp4")
    defer { try? FileManager.default.removeItem(at: silent) }
    try writeFrames(levels: levels, duration: Double(totalFrames) / rate, to: silent, style: style)
    if cancelled { throw MediaError.cancelled }
    try MediaMux.mux(video: silent, videoOffset: .zero, audio: audio, trim: nil, output: output)
    progress = 1
  }

  // MARK: - Phổ theo khung hình

  /// levels[khung][cột] 0..1.
  static func spectrum(
    file: AVAudioFile, frames: Int, rate: Double, onProgress: (Double) -> Bool
  ) throws -> [[Float]] {
    let n = fftSize
    let log2n = vDSP_Length(log2(Double(n)))
    guard let setup = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2)) else {
      throw MediaError.failed("không tạo được FFT")
    }
    defer { vDSP_destroy_fftsetup(setup) }
    var window = [Float](repeating: 0, count: n)
    vDSP_hann_window(&window, vDSP_Length(n), Int32(vDSP_HANN_NORM))

    // Cột i: dải tần log từ 60 Hz tới 12 kHz.
    let lo = 60.0, hi = min(12000.0, rate / 2 * 0.95)
    var edges = [Int]()
    for i in 0...bars {
      let f = lo * pow(hi / lo, Double(i) / Double(bars))
      edges.append(max(1, min(n / 2 - 1, Int(f / rate * Double(n)))))
    }

    // Đọc cả file về mono float (bài ≤ 15 phút: ≤ 170 MB float — đọc theo khối nhỏ để giới hạn).
    let format = file.processingFormat
    let hop = rate / Double(fps)
    var levels = [[Float]](repeating: [Float](repeating: 0, count: bars), count: frames)
    var smooth = [Float](repeating: 0, count: bars)
    var ring = [Float](repeating: 0, count: n)  // n mẫu gần nhất (mono)
    var filled = 0
    let chunk = AVAudioFrameCount(8192)
    guard let buf = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: chunk) else {
      throw MediaError.failed("không cấp được bộ đệm")
    }
    var real = [Float](repeating: 0, count: n / 2)
    var imag = [Float](repeating: 0, count: n / 2)
    var mags = [Float](repeating: 0, count: n / 2)
    var frameIndex = 0
    var consumed = 0  // số mẫu đã đọc
    file.framePosition = 0
    while frameIndex < frames {
      try file.read(into: buf, frameCount: chunk)
      let got = Int(buf.frameLength)
      if got == 0 { break }
      let ch = Int(format.channelCount)
      guard let data = buf.floatChannelData else { break }
      for i in 0..<got {
        var s: Float = 0
        for c in 0..<ch { s += data[c][i] }
        ring[filled % n] = s / Float(ch)
        filled += 1
        consumed += 1
        // Tâm khung hình k ở mẫu k·hop: đủ n/2 mẫu sau tâm thì tính.
        while frameIndex < frames && Double(consumed) >= Double(frameIndex) * hop + Double(n / 2) {
          var x = [Float](repeating: 0, count: n)
          for j in 0..<n { x[j] = ring[(filled - n + j + n * 4) % n] * window[j] }
          x.withUnsafeBufferPointer { xp in
            real.withUnsafeMutableBufferPointer { rp in
              imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                xp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: n / 2) {
                  vDSP_ctoz($0, 2, &split, 1, vDSP_Length(n / 2))
                }
                vDSP_fft_zrip(setup, &split, 1, log2n, FFTDirection(FFT_FORWARD))
                vDSP_zvmags(&split, 1, &mags, 1, vDSP_Length(n / 2))
              }
            }
          }
          for b in 0..<bars {
            let a = edges[b], e = max(edges[b + 1], a + 1)
            var sum: Float = 0
            for k in a..<e { sum += mags[k] }
            let db = 10 * log10(max(sum / Float(e - a), 1e-12)) - 60  // ~ −60…0
            let v = max(0, min(1, (db + 50) / 50))
            // Lên nhanh, xuống chậm.
            smooth[b] = v > smooth[b] ? v : smooth[b] * 0.82 + v * 0.18
            levels[frameIndex][b] = smooth[b]
          }
          frameIndex += 1
          if frameIndex % 60 == 0 && !onProgress(Double(frameIndex) / Double(frames)) {
            return levels
          }
        }
      }
    }
    return levels
  }

  // MARK: - Vẽ khung hình

  private func writeFrames(levels: [[Float]], duration: Double, to url: URL, style: Style) throws {
    let w = Self.width, h = Self.height
    let writer = try AVAssetWriter(outputURL: url, fileType: .mp4)
    let input = AVAssetWriterInput(
      mediaType: .video,
      outputSettings: [
        AVVideoCodecKey: AVVideoCodecType.h264,
        AVVideoWidthKey: w,
        AVVideoHeightKey: h,
        AVVideoCompressionPropertiesKey: [
          AVVideoAverageBitRateKey: 5_000_000,
          AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
          AVVideoMaxKeyFrameIntervalKey: 60,
        ],
      ])
    input.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(
      assetWriterInput: input,
      sourcePixelBufferAttributes: [
        kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        kCVPixelBufferWidthKey as String: w,
        kCVPixelBufferHeightKey as String: h,
      ])
    guard writer.canAdd(input) else { throw MediaError.failed("không thêm được luồng hình") }
    writer.add(input)
    guard writer.startWriting() else {
      throw MediaError.failed(writer.error?.localizedDescription ?? "không ghi được video")
    }
    writer.startSession(atSourceTime: .zero)

    let background = Self.drawBackground(style: style)
    let accent = style.color.cgColor
    let space = CGColorSpaceCreateDeviceRGB()
    let bitmapInfo =
      CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue
    // Nền vẽ sẵn đúng định dạng / bước dòng của bộ đệm hình → mỗi khung chỉ cần chép (nhanh hơn vẽ
    // lại ảnh 1080×1920).
    var backgroundBytes: [UInt8]? = nil
    for (i, frame) in levels.enumerated() {
      if cancelled { break }
      while !input.isReadyForMoreMediaData {
        if writer.status == .failed { break }
        Thread.sleep(forTimeInterval: 0.002)
      }
      guard let pool = adaptor.pixelBufferPool else { throw MediaError.failed("không có bộ đệm hình") }
      var pb: CVPixelBuffer?
      CVPixelBufferPoolCreatePixelBuffer(nil, pool, &pb)
      guard let pixel = pb else { throw MediaError.failed("không cấp được khung hình") }
      CVPixelBufferLockBaseAddress(pixel, [])
      let rowBytes = CVPixelBufferGetBytesPerRow(pixel)
      if backgroundBytes == nil {
        var bytes = [UInt8](repeating: 0, count: rowBytes * h)
        bytes.withUnsafeMutableBytes { raw in
          let bg = CGContext(
            data: raw.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: rowBytes,
            space: space, bitmapInfo: bitmapInfo)
          bg?.draw(background, in: CGRect(x: 0, y: 0, width: w, height: h))
        }
        backgroundBytes = bytes
      }
      if let base = CVPixelBufferGetBaseAddress(pixel) {
        backgroundBytes!.withUnsafeBytes { src in
          base.copyMemory(from: src.baseAddress!, byteCount: rowBytes * h)
        }
      }
      if let ctx = CGContext(
        data: CVPixelBufferGetBaseAddress(pixel), width: w, height: h, bitsPerComponent: 8,
        bytesPerRow: rowBytes, space: space, bitmapInfo: bitmapInfo)
      {
        ctx.textMatrix = .identity
        let t = Double(i) / Double(Self.fps)
        Self.drawBars(ctx, levels: frame, accent: accent)
        Self.drawProgress(ctx, t: t, duration: duration, accent: accent)
      }
      CVPixelBufferUnlockBaseAddress(pixel, [])
      if !adaptor.append(pixel, withPresentationTime: CMTime(value: CMTimeValue(i), timescale: Self.fps)) {
        break
      }
      if i % 15 == 0 { progress = 0.25 + 0.7 * Double(i) / Double(levels.count) }
    }
    input.markAsFinished()
    let done = DispatchSemaphore(value: 0)
    writer.finishWriting { done.signal() }
    done.wait()
    if cancelled { throw MediaError.cancelled }
    guard writer.status == .completed else {
      throw MediaError.failed(writer.error?.localizedDescription ?? "ghi video lỗi")
    }
  }

  /// Nền + tên bài vẽ một lần (gốc toạ độ CoreGraphics ở dưới-trái).
  static func drawBackground(style: Style) -> CGImage {
    let w = width, h = height
    let space = CGColorSpaceCreateDeviceRGB()
    let ctx = CGContext(
      data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0, space: space,
      bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue)!
    var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
    style.color.getRed(&r, green: &g, blue: &b, alpha: &a)
    let top = CGColor(red: r * 0.55, green: g * 0.55, blue: b * 0.55, alpha: 1)
    let bottom = CGColor(red: 0.04, green: 0.03, blue: 0.06, alpha: 1)
    let gradient = CGGradient(colorsSpace: space, colors: [bottom, top] as CFArray, locations: [0, 1])!
    ctx.textMatrix = .identity
    ctx.drawLinearGradient(
      gradient, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 0, y: h), options: [])
    drawText(ctx, style.title, size: 76, weight: .bold, centerY: CGFloat(h) * 0.78, alpha: 1)
    drawText(ctx, style.subtitle, size: 40, weight: .regular, centerY: CGFloat(h) * 0.78 - 110, alpha: 0.75)
    drawText(ctx, "ChuẩnTone", size: 34, weight: .semibold, centerY: 120, alpha: 0.6)
    return ctx.makeImage()!
  }

  /// Chữ căn giữa, tối đa 2 dòng, rộng 86% khung.
  static func drawText(
    _ ctx: CGContext, _ text: String, size: CGFloat, weight: UIFont.Weight, centerY: CGFloat,
    alpha: CGFloat
  ) {
    guard !text.isEmpty else { return }
    let font = UIFont.systemFont(ofSize: size, weight: weight)
    let para = NSMutableParagraphStyle()
    para.alignment = .center
    para.lineBreakMode = .byTruncatingTail
    let attr = NSAttributedString(
      string: text,
      attributes: [
        .font: font, .foregroundColor: UIColor(white: 1, alpha: alpha), .paragraphStyle: para,
      ])
    let boxW = CGFloat(width) * 0.86
    let boxH = font.lineHeight * 2.2
    let rect = CGRect(x: (CGFloat(width) - boxW) / 2, y: centerY - boxH / 2, width: boxW, height: boxH)
    let frameSetter = CTFramesetterCreateWithAttributedString(attr)
    let fit = CTFramesetterSuggestFrameSizeWithConstraints(
      frameSetter, CFRange(location: 0, length: 0), nil, CGSize(width: boxW, height: boxH), nil)
    let textRect = CGRect(
      x: rect.minX, y: rect.midY - fit.height / 2, width: boxW, height: fit.height)
    let frame = CTFramesetterCreateFrame(
      frameSetter, CFRange(location: 0, length: 0), CGPath(rect: textRect, transform: nil), nil)
    CTFrameDraw(frame, ctx)
  }

  /// Cột sóng nhạc đối xứng quanh giữa khung.
  static func drawBars(_ ctx: CGContext, levels: [Float], accent: CGColor) {
    let w = CGFloat(width), h = CGFloat(height)
    let n = levels.count
    let gap: CGFloat = 8
    let barW = (w * 0.86 - gap * CGFloat(n - 1)) / CGFloat(n)
    let x0 = w * 0.07
    let midY = h * 0.47
    let maxH = h * 0.22
    ctx.setFillColor(accent)
    for (i, v) in levels.enumerated() {
      let bh = max(10, CGFloat(v) * maxH)
      let rect = CGRect(x: x0 + CGFloat(i) * (barW + gap), y: midY - bh, width: barW, height: 2 * bh)
      ctx.addPath(CGPath(roundedRect: rect, cornerWidth: barW / 2, cornerHeight: barW / 2, transform: nil))
    }
    ctx.fillPath()
  }

  static func drawProgress(_ ctx: CGContext, t: Double, duration: Double, accent: CGColor) {
    let w = CGFloat(width)
    let y: CGFloat = 300
    let x0 = w * 0.1, x1 = w * 0.9
    ctx.setFillColor(UIColor(white: 1, alpha: 0.25).cgColor)
    ctx.fill(CGRect(x: x0, y: y, width: x1 - x0, height: 8))
    ctx.setFillColor(accent)
    let p = duration > 0 ? min(1, max(0, t / duration)) : 0
    ctx.fill(CGRect(x: x0, y: y, width: (x1 - x0) * CGFloat(p), height: 8))
    func fmt(_ s: Double) -> String { String(format: "%d:%02d", Int(s) / 60, Int(s) % 60) }
    drawText(ctx, "\(fmt(t)) / \(fmt(duration))", size: 36, weight: .medium, centerY: y - 50, alpha: 0.8)
  }
}

enum MediaError: Error {
  case badInput(String)
  case failed(String)
  case cancelled

  var message: String {
    switch self {
    case .badInput(let m), .failed(let m): return m
    case .cancelled: return "đã huỷ"
    }
  }
}
