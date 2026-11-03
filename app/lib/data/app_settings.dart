import 'dart:io';
import 'dart:math';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'library_db.dart';

/// Lựa chọn xuất (Cài đặt → Xuất, M8 B4).
class ExportPrefs {
  const ExportPrefs({
    this.aacBitrate = 192000,
    this.hd720 = false,
    this.normalize = true,
  });

  /// Chất lượng âm thanh: 128 / 192 / 256 kbit/s.
  final int aacBitrate;

  /// Video 720p (nhẹ hơn khi gửi Zalo) thay vì 1080p.
  final bool hd720;

  /// "Cân bằng âm lượng cho mạng xã hội": chuẩn hoá −14 LUFS, đỉnh thật ≤ −1 dBTP.
  final bool normalize;

  static const bitrates = [128000, 192000, 256000];

  ExportPrefs copyWith({int? aacBitrate, bool? hd720, bool? normalize}) =>
      ExportPrefs(
        aacBitrate: aacBitrate ?? this.aacBitrate,
        hd720: hd720 ?? this.hd720,
        normalize: normalize ?? this.normalize,
      );

  @override
  bool operator ==(Object other) =>
      other is ExportPrefs &&
      other.aacBitrate == aacBitrate &&
      other.hd720 == hd720 &&
      other.normalize == normalize;

  @override
  int get hashCode => Object.hash(aacBitrate, hd720, normalize);

  @override
  String toString() =>
      'ExportPrefs($aacBitrate bit/s, 720p: $hd720, chuẩn hoá: $normalize)';
}

/// Hiệu ứng giọng ở chế độ soundcard (M8 B6).
enum SoundcardFx {
  /// Giữ vang / echo của soundcard (đã có sẵn trong tiếng micro): bản thu để preset Khô.
  soundcard,

  /// Dùng hiệu ứng của app (tắt vang của soundcard): có trong bản thu và lúc Nghe lại.
  app,
}

/// Cài đặt chung của app (bảng `settings` của [LibraryDb]; trước M8 là `settings.json`): máy chủ
/// tách giọng, lựa chọn "không hỏi lại" khi gửi bài, mã máy ngẫu nhiên (server dùng để giới hạn
/// số lần tách và giữ job riêng của máy); từ M8 B4: micro, tự bật nghe lại giọng, lựa chọn xuất;
/// B6: chế độ soundcard.
class AppSettings {
  AppSettings({this.baseDir});

  /// Thư mục CSDL; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  Future<Object?> _get(String key) => LibraryDb.getSetting(baseDir, key);

  Future<void> _set(String key, Object? value) =>
      LibraryDb.setSetting(baseDir, key, value);

  /// Địa chỉ máy chủ tách giọng, vd. "http://192.168.1.10:8000" (null = chưa đặt).
  Future<String?> separationServer() async {
    final v = await _get('separationServer');
    return v is String && v.isNotEmpty ? v : null;
  }

  Future<void> setSeparationServer(String? url) =>
      _set('separationServer', url);

  /// Người dùng đã chọn "Không hỏi lại" khi đồng ý gửi bài lên để tách giọng.
  Future<bool> separationConsent() async =>
      await _get('separationConsent') == true;

  Future<void> setSeparationConsent(bool remember) =>
      _set('separationConsent', remember ? true : null);

  /// Micro đã chọn (Cài đặt → Micro); mặc định để hệ điều hành chọn.
  Future<InputKind> inputPreference() async {
    final v = await _get('inputPreference');
    return v is int && v > 0 && v <= InputKind.usb.index
        ? InputKind.values[v]
        : InputKind.auto;
  }

  Future<void> setInputPreference(InputKind kind) =>
      _set('inputPreference', kind == InputKind.auto ? null : kind.index);

  /// Tự bật nghe lại giọng khi cắm tai nghe có dây / soundcard, tắt khi về loa ngoài (mặc định bật).
  Future<bool> autoMonitor() async => await _get('autoMonitor') != false;

  Future<void> setAutoMonitor(bool on) =>
      _set('autoMonitor', on ? null : false);

  /// Chế độ soundcard khi cắm thiết bị USB: null = chưa hỏi (cắm vào thì app hỏi một lần).
  Future<bool?> soundcardMode() async {
    final v = await _get('soundcardMode');
    return v is bool ? v : null;
  }

  Future<void> setSoundcardMode(bool on) => _set('soundcardMode', on);

  /// Hiệu ứng giọng ở chế độ soundcard (mặc định giữ vang của soundcard).
  Future<SoundcardFx> soundcardFx() async => await _get('soundcardFx') == 'app'
      ? SoundcardFx.app
      : SoundcardFx.soundcard;

  Future<void> setSoundcardFx(SoundcardFx fx) =>
      _set('soundcardFx', fx == SoundcardFx.app ? 'app' : null);

  Future<ExportPrefs> exportPrefs() async {
    final v = await _get('export');
    if (v is! Map) return const ExportPrefs();
    final rate = v['aacBitrate'];
    return ExportPrefs(
      aacBitrate: ExportPrefs.bitrates.contains(rate) ? rate as int : 192000,
      hd720: v['hd720'] == true,
      normalize: v['normalize'] != false,
    );
  }

  Future<void> setExportPrefs(ExportPrefs p) => _set('export', {
    'aacBitrate': p.aacBitrate,
    'hd720': p.hd720,
    'normalize': p.normalize,
  });

  /// Ghi nhật ký thử nghiệm (Cài đặt → Nâng cao, M8 B7).
  Future<bool> testLog() async => await _get('testLog') == true;

  Future<void> setTestLog(bool on) => _set('testLog', on ? true : null);

  /// Đã xong (hoặc bỏ qua) hướng dẫn lần đầu.
  Future<bool> onboardingDone() async => await _get('onboardingDone') == true;

  Future<void> setOnboardingDone(bool done) =>
      _set('onboardingDone', done ? true : null);

  /// Cho mẹo [id] hiện lại (Cài đặt → Trợ giúp).
  Future<void> resetTip(String id) => _set('tipShown.$id', null);

  /// Mẹo [id] (vd "sing": bong bóng mẹo ở Màn hát) chưa hiện lần nào → true và đánh dấu đã hiện;
  /// các lần sau false.
  Future<bool> firstTimeTip(String id) async {
    final key = 'tipShown.$id';
    if (await _get(key) == true) return false;
    await _set(key, true);
    return true;
  }

  /// Mã máy ngẫu nhiên 128 bit (tạo lần đầu, không gắn với danh tính người dùng).
  Future<String> deviceId() async {
    final v = await _get('deviceId');
    if (v is String && v.length >= 16) return v;
    final rng = Random.secure();
    final id = List.generate(
      16,
      (_) => rng.nextInt(256),
    ).map((b) => b.toRadixString(16).padLeft(2, '0')).join();
    await _set('deviceId', id);
    return id;
  }
}

/// Chuẩn hoá địa chỉ người dùng nhập: thêm "http://" nếu thiếu, bỏ "/" cuối. null nếu không hợp lệ.
String? normalizeServerUrl(String input) {
  var s = input.trim();
  if (s.isEmpty) return null;
  if (!s.contains('://')) s = 'http://$s';
  final uri = Uri.tryParse(s);
  if (uri == null ||
      !(uri.scheme == 'http' || uri.scheme == 'https') ||
      uri.host.isEmpty) {
    return null;
  }
  return uri
      .replace(path: '', query: null, fragment: null)
      .toString()
      .replaceAll(RegExp(r'/+$'), '');
}
