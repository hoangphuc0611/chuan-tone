// Đo khớp hình ↔ tiếng của video xuất (M7 B6/B7, chỉ tiêu ≤ 40 ms): tìm tiếng vỗ tay trong luồng
// âm thanh (đỉnh năng lượng 5 ms nổi hẳn so với nền), rồi xuất các khung hình quanh mỗi tiếng vỗ
// thành PNG để xem khung nào hai tay chạm nhau. Lệch = (thời điểm khung chạm) − (thời điểm tiếng vỗ).
//
//   swift tools/video/av_sync.swift video.mp4 thu_muc_ra [số_khung_mỗi_bên=6]
//
// Chỉ dùng AVFoundation của macOS (không ffmpeg).

import AVFoundation
import AppKit

let args = CommandLine.arguments
guard args.count >= 3 else {
  print("Cách dùng: swift av_sync.swift video.mp4 thu_muc_ra [so_khung_moi_ben]")
  exit(1)
}
let url = URL(fileURLWithPath: args[1])
let outDir = URL(fileURLWithPath: args[2])
let side = args.count > 3 ? Int(args[3]) ?? 6 : 6
try? FileManager.default.createDirectory(at: outDir, withIntermediateDirectories: true)

let asset = AVURLAsset(url: url)
guard let audioTrack = asset.tracks(withMediaType: .audio).first,
  let videoTrack = asset.tracks(withMediaType: .video).first
else {
  print("video thiếu luồng hình hoặc tiếng")
  exit(1)
}
let fps = Double(videoTrack.nominalFrameRate)
print(String(format: "hình %.0f×%.0f %.2f fps, dài %.2f s", videoTrack.naturalSize.width,
             videoTrack.naturalSize.height, fps, CMTimeGetSeconds(asset.duration)))

// ---- Tiếng: PCM float mono ----
let reader = try AVAssetReader(asset: asset)
let rate = 48000.0
let out = AVAssetReaderTrackOutput(
  track: audioTrack,
  outputSettings: [
    AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 32,
    AVLinearPCMIsFloatKey: true, AVLinearPCMIsNonInterleaved: false,
    AVNumberOfChannelsKey: 1, AVSampleRateKey: rate,
  ])
reader.add(out)
reader.startReading()
var pcm = [Float]()
while let sb = out.copyNextSampleBuffer(), let block = CMSampleBufferGetDataBuffer(sb) {
  let n = CMBlockBufferGetDataLength(block) / 4
  var chunk = [Float](repeating: 0, count: n)
  CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: n * 4, destination: &chunk)
  pcm += chunk
}

// Năng lượng theo khối 5 ms; tiếng vỗ = khối nổi > 12 dB so với trung vị 1 s trước, cách nhau ≥ 0,4 s.
let blk = Int(rate * 0.005)
var energy = [Float]()
var i = 0
while i + blk <= pcm.count {
  var e: Float = 0
  for k in i..<(i + blk) { e += pcm[k] * pcm[k] }
  energy.append(e / Float(blk))
  i += blk
}
var claps = [Double]()
var last = -1.0
for b in 200..<energy.count {
  let window = energy[(b - 200)..<b].sorted()
  let median = max(window[window.count / 2], 1e-9)
  let t = Double(b * blk) / rate
  if energy[b] > median * 16 && energy[b] > 1e-4 && t - last > 0.4 {
    // Mẫu đầu tiên trong khối vượt 30% đỉnh khối → thời điểm vỗ chính xác hơn.
    var peak: Float = 0
    for k in (b * blk)..<min(pcm.count, (b + 2) * blk) { peak = max(peak, abs(pcm[k])) }
    var onset = b * blk
    for k in (b * blk)..<min(pcm.count, (b + 2) * blk) where abs(pcm[k]) >= 0.3 * peak {
      onset = k
      break
    }
    claps.append(Double(onset) / rate)
    last = t
  }
}
print("tiếng vỗ (s): " + claps.map { String(format: "%.3f", $0) }.joined(separator: ", "))

// ---- Hình: khung quanh mỗi tiếng vỗ ----
let gen = AVAssetImageGenerator(asset: asset)
gen.requestedTimeToleranceBefore = .zero
gen.requestedTimeToleranceAfter = .zero
gen.appliesPreferredTrackTransform = true
gen.maximumSize = CGSize(width: 360, height: 640)
for (ci, t) in claps.enumerated() {
  let center = Int((t * fps).rounded())
  for f in (center - side)...(center + side) where f >= 0 {
    let time = CMTime(value: CMTimeValue(f), timescale: CMTimeScale(fps.rounded()))
    guard let img = try? gen.copyCGImage(at: time, actualTime: nil) else { continue }
    let rep = NSBitmapImageRep(cgImage: img)
    let ms = Int(((Double(f) / fps) - t) * 1000)
    let name = String(format: "vo%02d_khung%05d_%+04dms.png", ci, f, ms)
    try rep.representation(using: .png, properties: [:])?.write(to: outDir.appendingPathComponent(name))
  }
}
print("đã xuất khung hình vào \(outDir.path) — tên file ghi lệch (ms) của khung so với tiếng vỗ")
