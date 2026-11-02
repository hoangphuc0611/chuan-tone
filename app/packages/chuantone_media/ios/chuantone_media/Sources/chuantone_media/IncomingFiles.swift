import Flutter
import UIKit

/// "Mở bằng ChuẩnTone" (M8 B1): file nhạc từ app khác (Zalo, Tệp, Safari, AirDrop). Info.plist khai
/// báo kiểu file âm thanh (`CFBundleDocumentTypes`, không mở tại chỗ) nên iOS chép file vào
/// `Documents/Inbox` rồi gọi scene delegate với URL của nó.
///
/// File được chuyển / chép sang `tmp/incoming/<uuid>/<tên gốc>` (giữ tên gốc: khoá bài = tên +
/// kích thước) và xếp vào hàng chờ; Dart lấy bằng `take` (lúc khởi động và khi nhận `available`).
final class IncomingFiles: NSObject {
  private let channel: FlutterMethodChannel
  private let queue = DispatchQueue(label: "chuantone.incoming")
  private var pending: [[String: String]] = []  // chỉ dùng trên `queue`

  init(channel: FlutterMethodChannel) {
    self.channel = channel
    super.init()
    channel.setMethodCallHandler { [weak self] call, result in
      guard let self = self, call.method == "take" else { return result(FlutterMethodNotImplemented) }
      self.queue.async {
        let files = self.pending
        self.pending.removeAll()
        DispatchQueue.main.async { result(files) }
      }
    }
  }

  /// Nhận các URL; trả true nếu có ít nhất một file.
  @discardableResult
  func receive(_ urls: [URL]) -> Bool {
    let files = urls.filter { $0.isFileURL }
    if files.isEmpty { return false }
    queue.async {
      for url in files {
        if let entry = Self.stage(url) { self.pending.append(entry) }
      }
      DispatchQueue.main.async { self.channel.invokeMethod("available", arguments: nil) }
    }
    return true
  }

  private static func stage(_ url: URL) -> [String: String]? {
    let access = url.startAccessingSecurityScopedResource()
    defer { if access { url.stopAccessingSecurityScopedResource() } }
    let fm = FileManager.default
    let name = url.lastPathComponent
    let dir = fm.temporaryDirectory
      .appendingPathComponent("incoming", isDirectory: true)
      .appendingPathComponent(UUID().uuidString, isDirectory: true)
    let dest = dir.appendingPathComponent(name)
    do {
      try fm.createDirectory(at: dir, withIntermediateDirectories: true)
      // File trong Documents/Inbox là bản iOS chép cho app → chuyển luôn (không để lại rác).
      let inbox = fm.urls(for: .documentDirectory, in: .userDomainMask).first?
        .appendingPathComponent("Inbox", isDirectory: true).standardizedFileURL.path
      if let inbox = inbox, url.standardizedFileURL.path.hasPrefix(inbox + "/") {
        try fm.moveItem(at: url, to: dest)
      } else {
        try fm.copyItem(at: url, to: dest)
      }
      return ["path": dest.path, "name": name]
    } catch {
      try? fm.removeItem(at: dir)
      return nil
    }
  }
}
