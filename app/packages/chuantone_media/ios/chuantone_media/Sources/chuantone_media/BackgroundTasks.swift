import UIKit

/// Chạy nền ngắn (M8 B7): xin iOS cho chạy tiếp một lúc (thường ~30 giây) khi app vào nền giữa lúc
/// xuất / tách giọng. Hết giờ iOS gọi expirationHandler → kết thúc việc nền; phần Dart thấy lỗi mạng
/// / việc bị ngắt và tự tiếp tục khi người dùng quay lại app. Gọi trên luồng chính.
final class BackgroundTasks {
  private var tasks: [Int: UIBackgroundTaskIdentifier] = [:]
  private var next = 1

  /// Trả mã việc (> 0), hoặc 0 nếu iOS không cho chạy nền lúc này.
  func begin(name: String) -> Int {
    let id = next
    next += 1
    let task = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in
      self?.end(id)
    }
    if task == .invalid { return 0 }
    tasks[id] = task
    return id
  }

  func end(_ id: Int) {
    guard let task = tasks.removeValue(forKey: id) else { return }
    UIApplication.shared.endBackgroundTask(task)
  }
}
