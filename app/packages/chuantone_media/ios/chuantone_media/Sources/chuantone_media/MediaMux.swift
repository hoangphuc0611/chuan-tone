import AVFoundation

/// Ghép hình (không tiếng) + tiếng thành MP4 bằng AVMutableComposition + xuất nguyên dạng (không nén
/// lại hình lẫn tiếng → nhanh, giữ chất lượng). Dùng cho video sóng nhạc (B5) và video camera (B6).
enum MediaMux {
  /// - videoOffset: thời điểm (trên trục của file tiếng) mà khung hình đầu tiên khớp; âm = hình bắt
  ///   đầu trước tiếng (phần thừa bị cắt).
  /// - trim: đoạn của file tiếng giữ lại (nil = cả file); hình cắt theo cùng trục.
  static func mux(video: URL, videoOffset: CMTime, audio: URL, trim: CMTimeRange?, output: URL)
    throws
  {
    let audioAsset = AVURLAsset(url: audio)
    let videoAsset = AVURLAsset(url: video)
    guard let audioTrack = audioAsset.tracks(withMediaType: .audio).first else {
      throw MediaError.badInput("file tiếng không có luồng âm thanh")
    }
    guard let videoTrack = videoAsset.tracks(withMediaType: .video).first else {
      throw MediaError.badInput("file hình không có luồng video")
    }
    let audioRange = trim ?? CMTimeRange(start: .zero, duration: audioAsset.duration)
    let comp = AVMutableComposition()
    guard
      let a = comp.addMutableTrack(withMediaType: .audio, preferredTrackID: kCMPersistentTrackID_Invalid),
      let v = comp.addMutableTrack(withMediaType: .video, preferredTrackID: kCMPersistentTrackID_Invalid)
    else { throw MediaError.failed("không tạo được bản ghép") }
    try a.insertTimeRange(audioRange, of: audioTrack, at: .zero)

    // Khung hình ở thời điểm t của file hình ↔ thời điểm t + videoOffset của file tiếng
    // ↔ thời điểm t + videoOffset − trim.start của video xuất.
    let videoDuration = videoAsset.duration
    let srcStart = CMTimeMaximum(.zero, CMTimeSubtract(audioRange.start, videoOffset))
    let srcEnd = CMTimeMinimum(videoDuration, CMTimeSubtract(audioRange.end, videoOffset))
    if CMTimeCompare(srcEnd, srcStart) > 0 {
      let at = CMTimeMaximum(.zero, CMTimeSubtract(videoOffset, audioRange.start))
      try v.insertTimeRange(
        CMTimeRange(start: srcStart, end: srcEnd), of: videoTrack, at: at)
    }
    v.preferredTransform = videoTrack.preferredTransform

    let tmp = output.deletingLastPathComponent()
      .appendingPathComponent(".ghep-\(UUID().uuidString).mp4")
    defer { try? FileManager.default.removeItem(at: tmp) }
    guard let export = AVAssetExportSession(asset: comp, presetName: AVAssetExportPresetPassthrough)
    else { throw MediaError.failed("không xuất được bản ghép") }
    export.outputURL = tmp
    export.outputFileType = .mp4
    export.shouldOptimizeForNetworkUse = true  // moov ở đầu file: đăng mạng xã hội nhanh hơn
    let done = DispatchSemaphore(value: 0)
    export.exportAsynchronously { done.signal() }
    done.wait()
    guard export.status == .completed else {
      throw MediaError.failed(export.error?.localizedDescription ?? "ghép video lỗi")
    }
    try? FileManager.default.removeItem(at: output)
    try FileManager.default.moveItem(at: tmp, to: output)
  }
}
