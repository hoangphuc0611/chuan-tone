import 'package:chuantone_media/chuantone_media.dart';

/// Chạy [body] trong một lần chạy nền ngắn (M8 B7, docs/m8-plan.md mục 5): app vào nền giữa chừng
/// (khoá máy, chuyển app) thì iOS cho chạy tiếp một lúc thay vì dừng ngay. Kênh native lỗi / không
/// có thì vẫn chạy [body] bình thường. [begin] / [end]: test thay bằng hàm giả.
Future<T> inBackground<T>(
  String name,
  Future<T> Function() body, {
  Future<int?> Function(String name) begin = ChuanToneMedia.beginBackground,
  Future<void> Function(int id) end = ChuanToneMedia.endBackground,
}) async {
  int? id;
  try {
    id = await begin(name);
  } catch (_) {
    // không xin được: vẫn làm, chỉ là vào nền thì có thể bị dừng
  }
  try {
    return await body();
  } finally {
    if (id != null) {
      try {
        await end(id);
      } catch (_) {}
    }
  }
}
