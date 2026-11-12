import AVFoundation
import Accelerate
import CoreGraphics
import CoreText
import UIKit

/// Dựng video 9:16 nền sóng nhạc + tên bài từ file âm thanh (M7 B5, docs/m7-plan.md mục 3):
///  1. Đọc âm thanh (AVAudioFile) → mỗi khung hình 1/30 s: phổ 2048 điểm (vDSP) gom 16 dải theo
///     thang log 60 Hz – 12 kHz, làm mượt lên nhanh / xuống chậm.
///  2. Vẽ từng khung (CoreGraphics trên CVPixelBuffer): nền, đĩa nhạc, tên bài, thương hiệu vẽ sẵn
///     một lần (giao diện mới, khung Figma 9:16); mỗi khung vẽ 32 cột sóng đối xứng (dải trầm ở
///     giữa), thanh thời gian → AVAssetWriter H.264 1080×1920, 30 fps, không tiếng.
///  3. Ghép hình + chính file âm thanh (AVMutableComposition, xuất nguyên dạng — không nén lại) →
///     MP4. Hình dựng từ đúng file tiếng nên khớp tiếng tuyệt đối.
final class WaveformVideo {
  static let width = 1080
  static let height = 1920
  static let fps: Int32 = 30
  private static let bars = 16  // dải tần; vẽ đối xứng thành 32 cột
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

  func render(audio: URL, output: URL, style: Style, hd720: Bool = false) throws {
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
    try MediaMux.mux(
      video: silent, videoOffset: .zero, audio: audio, trim: nil, output: output, hd720: hd720)
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
    let palette = Palette(style.color)
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
        Self.drawBars(ctx, levels: frame, palette: palette)
        Self.drawProgress(ctx, t: t, duration: duration, palette: palette)
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

  // MARK: Bố cục (theo khung Figma "Khung Video 9:16 - Sóng Nhạc", docs/ui/redesign-plan.md)
  // Toạ độ y tính từ trên xuống cho dễ đọc, đổi sang CoreGraphics (gốc dưới-trái) bằng `up(_:)`.
  // Không vẽ bóng đổ / làm mờ mỗi khung để xuất nhanh: mọi thứ tĩnh vẽ sẵn trong nền.

  private static let ringY: CGFloat = 660  // tâm đĩa nhạc + sóng
  private static let ringRadius: CGFloat = 440
  private static let barSpan: CGFloat = 780  // bề ngang cả dãy cột
  private static let barMaxHalf: CGFloat = 250  // nửa chiều cao cột to nhất
  private static let titleTop: CGFloat = 1130
  private static let progressY: CGFloat = 1560
  private static let dividerY: CGFloat = 1665
  private static let brandY: CGFloat = 1750
  private static let marginX: CGFloat = CGFloat(width) * 0.12
  private static let gold = UIColor(red: 1, green: 0.71, blue: 0.28, alpha: 1)  // #FFB548

  private static func up(_ y: CGFloat) -> CGFloat { CGFloat(height) - y }

  /// Màu dùng chung cho các khung hình, tạo một lần từ màu thể loại.
  struct Palette {
    let accent: (r: CGFloat, g: CGFloat, b: CGFloat)
    /// Cột sóng: đầu cột màu thể loại nhạt, giữa gần trắng.
    let bars: CGGradient
    let progress: CGColor

    init(_ color: UIColor) {
      var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
      color.getRed(&r, green: &g, blue: &b, alpha: &a)
      accent = (r, g, b)
      let space = CGColorSpaceCreateDeviceRGB()
      let tip = Palette.mix((r, g, b), white: 0.2, alpha: 1)
      let mid = Palette.mix((r, g, b), white: 0.82, alpha: 1)
      bars = CGGradient(
        colorsSpace: space, colors: [tip, mid, tip] as CFArray, locations: [0, 0.5, 1])!
      progress = Palette.mix((r, g, b), white: 0.35, alpha: 1)
    }

    /// Pha màu thể loại với trắng (t > 0) hoặc đen (t < 0).
    static func mix(
      _ c: (r: CGFloat, g: CGFloat, b: CGFloat), white t: CGFloat, alpha: CGFloat
    ) -> CGColor {
      func m(_ v: CGFloat) -> CGFloat { t >= 0 ? v + (1 - v) * t : v * (1 + t) }
      return CGColor(red: m(c.r), green: m(c.g), blue: m(c.b), alpha: alpha)
    }

    func color(white t: CGFloat = 0, alpha: CGFloat = 1) -> CGColor {
      Palette.mix(accent, white: t, alpha: alpha)
    }
  }

  /// Phần tĩnh vẽ một lần: nền tối ngả màu thể loại, quầng sáng, đĩa nhạc, tên bài, nhãn "Bản thu
  /// bởi ChuẩnTone", rãnh thanh thời gian, thương hiệu dưới cùng.
  static func drawBackground(style: Style) -> CGImage {
    let w = CGFloat(width), h = CGFloat(height)
    let space = CGColorSpaceCreateDeviceRGB()
    let ctx = CGContext(
      data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
      bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue)!
    ctx.textMatrix = .identity
    let pal = Palette(style.color)
    let center = CGPoint(x: w / 2, y: up(ringY))

    // 1. Nền: dưới gần đen, trên ngả màu thể loại (giống nền sân khấu của Màn hát).
    let bottom = CGColor(red: 0.043, green: 0.031, blue: 0.063, alpha: 1)
    let middle = CGColor(red: 0.082, green: 0.067, blue: 0.102, alpha: 1)  // #15111A
    let top = pal.color(white: -0.62)
    let bg = CGGradient(
      colorsSpace: space, colors: [bottom, middle, top] as CFArray, locations: [0, 0.45, 1])!
    ctx.drawLinearGradient(bg, start: .zero, end: CGPoint(x: 0, y: h), options: [])

    // 2. Quầng sáng sau đĩa + vệt đỏ thương hiệu ở bên trái phía dưới.
    func glow(_ c: CGColor, at p: CGPoint, radius: CGFloat) {
      let clear = c.copy(alpha: 0)!
      let g = CGGradient(colorsSpace: space, colors: [c, clear] as CFArray, locations: [0, 1])!
      ctx.drawRadialGradient(
        g, startCenter: p, startRadius: 0, endCenter: p, endRadius: radius, options: [])
    }
    glow(pal.color(alpha: 0.5), at: center, radius: 760)
    glow(CGColor(red: 0.69, green: 0.13, blue: 0.18, alpha: 0.22), at: CGPoint(x: w * 0.1, y: up(1250)), radius: 620)

    // 3. Đĩa nhạc: các vòng tối có viền sáng, lõi màu thể loại.
    func disc(_ r: CGFloat, fill: CGColor, stroke: CGColor, lineWidth: CGFloat) {
      let rect = CGRect(x: center.x - r, y: center.y - r, width: 2 * r, height: 2 * r)
      ctx.setFillColor(fill)
      ctx.fillEllipse(in: rect)
      ctx.setStrokeColor(stroke)
      ctx.setLineWidth(lineWidth)
      ctx.strokeEllipse(in: rect)
    }
    let shade = CGColor(gray: 0, alpha: 0.3)
    disc(ringRadius, fill: shade, stroke: CGColor(gray: 1, alpha: 0.08), lineWidth: 3)
    ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.045))
    ctx.setLineWidth(2)
    for r in stride(from: ringRadius - 40, to: 330, by: -26) {
      ctx.strokeEllipse(in: CGRect(x: center.x - r, y: center.y - r, width: 2 * r, height: 2 * r))
    }
    disc(320, fill: CGColor(gray: 0, alpha: 0.35), stroke: pal.color(white: 0.2, alpha: 0.4), lineWidth: 3)
    ctx.saveGState()
    let core: CGFloat = 230
    ctx.addEllipse(in: CGRect(x: center.x - core, y: center.y - core, width: 2 * core, height: 2 * core))
    ctx.clip()
    let coreGradient = CGGradient(
      colorsSpace: space, colors: [pal.color(alpha: 0.95), pal.color(white: -0.55)] as CFArray,
      locations: [0, 1])!
    ctx.drawRadialGradient(
      coreGradient, startCenter: center, startRadius: 0, endCenter: center, endRadius: core,
      options: [])
    ctx.restoreGState()
    ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.14))
    ctx.strokeEllipse(in: CGRect(x: center.x - core, y: center.y - core, width: 2 * core, height: 2 * core))

    // 4. Vài đốm sáng nhỏ cố định.
    let sparks: [(CGFloat, CGFloat, CGFloat, CGFloat)] = [
      (0.08, 260, 5, 0.35), (0.9, 330, 4, 0.3), (0.82, 980, 6, 0.25), (0.14, 1010, 4, 0.3),
      (0.95, 760, 5, 0.2), (0.05, 1480, 4, 0.25), (0.88, 1440, 3, 0.3), (0.5, 175, 3, 0.25),
    ]
    for (fx, y, r, a) in sparks {
      ctx.setFillColor(CGColor(gray: 1, alpha: a))
      ctx.fillEllipse(in: CGRect(x: w * fx - r, y: up(y) - r, width: 2 * r, height: 2 * r))
    }

    // 5. Tên bài (to; dài thì nhỏ lại, tối đa 2 dòng) + dòng phụ màu vàng.
    var y = titleTop
    let big = UIFont.systemFont(ofSize: 84, weight: .bold)
    let title = style.title.isEmpty ? "Bản thu của tôi" : style.title
    if lineWidth(title, font: big) <= w * 0.84 {
      y += drawCentered(ctx, title, font: big, color: .white, top: y)
    } else {
      y += drawCentered(
        ctx, title, font: UIFont.systemFont(ofSize: 68, weight: .bold), color: .white, top: y,
        maxLines: 2)
    }
    if !style.subtitle.isEmpty {
      y += 18
      y += drawCentered(
        ctx, style.subtitle, font: UIFont.systemFont(ofSize: 42, weight: .semibold), color: gold,
        top: y)
    }

    // 6. Nhãn viên thuốc "Bản thu bởi ChuẩnTone".
    y += 40
    let pillFont = UIFont.systemFont(ofSize: 34, weight: .medium)
    let pillText = "Bản thu bởi ChuẩnTone"
    let pillW = lineWidth(pillText, font: pillFont) + 88
    let pillH: CGFloat = 80
    let pill = CGRect(x: (w - pillW) / 2, y: up(y + pillH), width: pillW, height: pillH)
    ctx.addPath(CGPath(roundedRect: pill, cornerWidth: pillH / 2, cornerHeight: pillH / 2, transform: nil))
    ctx.setFillColor(CGColor(gray: 1, alpha: 0.12))
    ctx.fillPath()
    drawLine(
      ctx, pillText, font: pillFont, color: UIColor(white: 1, alpha: 0.92), x: pill.minX + 44,
      baseline: y + pillH / 2 + pillFont.capHeight / 2)

    // 7. Rãnh thanh thời gian (phần đã chạy vẽ mỗi khung).
    let track = CGRect(x: marginX, y: up(progressY) - 3, width: w - 2 * marginX, height: 6)
    ctx.addPath(CGPath(roundedRect: track, cornerWidth: 3, cornerHeight: 3, transform: nil))
    ctx.setFillColor(CGColor(gray: 1, alpha: 0.18))
    ctx.fillPath()

    // 8. Vạch ngăn + thương hiệu: biểu tượng đĩa, "ChuẩnTone", "Bấm bài là hát".
    ctx.setFillColor(CGColor(gray: 1, alpha: 0.1))
    ctx.fill(CGRect(x: marginX, y: up(dividerY) - 1, width: w - 2 * marginX, height: 2))
    let nameFont = UIFont.systemFont(ofSize: 46, weight: .bold)
    let sloganFont = UIFont.systemFont(ofSize: 30, weight: .medium)
    let logoR: CGFloat = 38
    let textW = max(lineWidth("ChuẩnTone", font: nameFont), lineWidth("Bấm bài là hát", font: sloganFont))
    let x0 = (w - (2 * logoR + 24 + textW)) / 2
    let logo = CGPoint(x: x0 + logoR, y: up(brandY))
    ctx.saveGState()
    ctx.addEllipse(in: CGRect(x: logo.x - logoR, y: logo.y - logoR, width: 2 * logoR, height: 2 * logoR))
    ctx.clip()
    let brand = CGGradient(
      colorsSpace: space,
      colors: [
        CGColor(red: 0.69, green: 0.13, blue: 0.18, alpha: 1),  // #B1212D
        CGColor(red: 0.83, green: 0.24, blue: 0.26, alpha: 1),  // #D43C42
        CGColor(red: 1, green: 0.71, blue: 0.28, alpha: 1),  // #FFB548
      ] as CFArray, locations: [0, 0.55, 1])!
    ctx.drawLinearGradient(
      brand, start: CGPoint(x: logo.x - logoR, y: logo.y + logoR),
      end: CGPoint(x: logo.x + logoR, y: logo.y - logoR), options: [])
    ctx.restoreGState()
    ctx.setFillColor(CGColor(red: 0.082, green: 0.067, blue: 0.102, alpha: 1))
    ctx.fillEllipse(in: CGRect(x: logo.x - 15, y: logo.y - 15, width: 30, height: 30))
    ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.9))
    ctx.setLineWidth(4)
    ctx.strokeEllipse(in: CGRect(x: logo.x - 15, y: logo.y - 15, width: 30, height: 30))
    let tx = x0 + 2 * logoR + 24
    drawLine(ctx, "ChuẩnTone", font: nameFont, color: .white, x: tx, baseline: brandY - 4)
    drawLine(ctx, "Bấm bài là hát", font: sloganFont, color: gold, x: tx, baseline: brandY + 36)
    return ctx.makeImage()!
  }

  private static func attributed(_ text: String, font: UIFont, color: UIColor) -> NSAttributedString {
    NSAttributedString(string: text, attributes: [.font: font, .foregroundColor: color])
  }

  /// Độ rộng một dòng chữ (không tính khoảng trắng cuối).
  static func lineWidth(_ text: String, font: UIFont) -> CGFloat {
    let line = CTLineCreateWithAttributedString(attributed(text, font: font, color: .white))
    return CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil) - CTLineGetTrailingWhitespaceWidth(line))
  }

  /// Một dòng chữ căn trái tại [x], đường chân chữ ở [baseline] (tính từ trên xuống).
  static func drawLine(
    _ ctx: CGContext, _ text: String, font: UIFont, color: UIColor, x: CGFloat, baseline: CGFloat
  ) {
    ctx.textPosition = CGPoint(x: x, y: up(baseline))
    CTLineDraw(CTLineCreateWithAttributedString(attributed(text, font: font, color: color)), ctx)
  }

  /// Chữ căn giữa bắt đầu từ [top] (tính từ trên xuống), tự xuống dòng tối đa [maxLines], còn thừa
  /// thì dòng cuối cắt bằng "…". Trả về chiều cao đã dùng.
  @discardableResult
  static func drawCentered(
    _ ctx: CGContext, _ text: String, font: UIFont, color: UIColor, top: CGFloat, maxLines: Int = 1
  ) -> CGFloat {
    guard !text.isEmpty else { return 0 }
    let attr = attributed(text, font: font, color: color)
    let boxW = Double(CGFloat(width) * 0.84)
    let typesetter = CTTypesetterCreateWithAttributedString(attr)
    var lines: [CTLine] = []
    var start = 0
    while start < attr.length && lines.count < maxLines {
      let count = CTTypesetterSuggestLineBreak(typesetter, start, boxW)
      if count <= 0 { break }
      var line = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: count))
      if lines.count == maxLines - 1 && start + count < attr.length {
        let rest = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: attr.length - start))
        let dots = CTLineCreateWithAttributedString(attributed("…", font: font, color: color))
        line = CTLineCreateTruncatedLine(rest, boxW, .end, dots) ?? line
      }
      lines.append(line)
      start += count
    }
    var baseline = top + font.ascender
    for line in lines {
      let lw = CTLineGetTypographicBounds(line, nil, nil, nil) - CTLineGetTrailingWhitespaceWidth(line)
      ctx.textPosition = CGPoint(x: (CGFloat(width) - CGFloat(lw)) / 2, y: up(baseline))
      CTLineDraw(line, ctx)
      baseline += font.lineHeight
    }
    return font.lineHeight * CGFloat(lines.count)
  }

  /// Cột sóng nhạc mỗi khung: [levels] là các dải tần từ trầm tới cao; vẽ đối xứng — dải trầm ở
  /// giữa, cao dần ra hai bên — và đối xứng trên / dưới quanh tâm đĩa. Tô dải màu qua vùng cắt.
  static func drawBars(_ ctx: CGContext, levels: [Float], palette: Palette) {
    let n = levels.count
    guard n > 0 else { return }
    let w = CGFloat(width)
    let cy = up(ringY)
    let pitch = barSpan / CGFloat(2 * n)
    let barW = pitch * 0.6
    ctx.saveGState()
    for (i, v) in levels.enumerated() {
      let half = max(barW / 2 + 2, CGFloat(v) * barMaxHalf)
      for side: CGFloat in [-1, 1] {
        let cx = w / 2 + side * (CGFloat(i) + 0.5) * pitch
        let rect = CGRect(x: cx - barW / 2, y: cy - half, width: barW, height: 2 * half)
        ctx.addPath(CGPath(roundedRect: rect, cornerWidth: barW / 2, cornerHeight: barW / 2, transform: nil))
      }
    }
    ctx.clip()
    ctx.drawLinearGradient(
      palette.bars, start: CGPoint(x: 0, y: cy - barMaxHalf), end: CGPoint(x: 0, y: cy + barMaxHalf),
      options: [])
    ctx.restoreGState()
  }

  /// Phần đã chạy của thanh thời gian + núm + "phút:giây / tổng".
  static func drawProgress(_ ctx: CGContext, t: Double, duration: Double, palette: Palette) {
    let w = CGFloat(width)
    let x0 = marginX, x1 = w - marginX
    let p = duration > 0 ? CGFloat(min(1, max(0, t / duration))) : 0
    let y = up(progressY)
    let done = CGRect(x: x0, y: y - 3, width: max(6, (x1 - x0) * p), height: 6)
    ctx.addPath(CGPath(roundedRect: done, cornerWidth: 3, cornerHeight: 3, transform: nil))
    ctx.setFillColor(palette.progress)
    ctx.fillPath()
    let knob = x0 + (x1 - x0) * p
    ctx.setFillColor(CGColor(gray: 1, alpha: 1))
    ctx.fillEllipse(in: CGRect(x: knob - 11, y: y - 11, width: 22, height: 22))
    func fmt(_ s: Double) -> String { String(format: "%d:%02d", Int(s) / 60, Int(s) % 60) }
    drawCentered(
      ctx, "\(fmt(t)) / \(fmt(duration))", font: UIFont.systemFont(ofSize: 32, weight: .medium),
      color: UIColor(white: 1, alpha: 0.7), top: progressY + 26)
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
