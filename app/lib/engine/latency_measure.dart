import 'dart:io';

import 'package:device_info_plus/device_info_plus.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/latency_history.dart';

/// Thứ tự cột CSV của lịch sử đo độ trễ – giữ khớp với tools/latency/aggregate.py.
const latencyCsvHeader =
    'date,brand,model,os,route,api,sample_rate,burst_frames,buffer_frames,'
    'measured_ms,reported_ms,confidence,valid_bursts,spread_ms';

/// Tên máy cho báo cáo độ trễ.
class DeviceLabel {
  const DeviceLabel({this.brand = '', this.model = '', this.os = ''});

  final String brand;
  final String model;
  final String os;

  /// Đọc tên máy; lỗi (thiếu plugin…) → nhãn rỗng, không chặn việc đo.
  static Future<DeviceLabel> load() async {
    try {
      final info = DeviceInfoPlugin();
      if (Platform.isAndroid) {
        final a = await info.androidInfo;
        return DeviceLabel(
          brand: a.manufacturer,
          model: a.model,
          os: 'Android ${a.version.release} (API ${a.version.sdkInt})',
        );
      }
      if (Platform.isIOS) {
        final i = await info.iosInfo;
        return DeviceLabel(
          brand: 'Apple',
          model: i.utsname.machine,
          os: 'iOS ${i.systemVersion}',
        );
      }
    } catch (_) {}
    return const DeviceLabel();
  }
}

/// Một dòng CSV cho kết quả đo [r].
String latencyCsv(
  LatencyResult r,
  EngineMetrics m,
  DeviceLabel d, {
  DateTime? now,
}) {
  String q(String s) => '"${s.replaceAll('"', "'")}"';
  return [
    (now ?? DateTime.now()).toIso8601String(),
    q(d.brand),
    q(d.model),
    q(d.os),
    r.route.name,
    m.audioApi.name,
    m.sampleRate,
    r.burstFrames,
    r.bufferFrames,
    r.latencyMs.toStringAsFixed(2),
    r.reportedRoundTripMs.toStringAsFixed(2),
    r.confidence.toStringAsFixed(2),
    r.validBursts,
    r.spreadMs.toStringAsFixed(3),
  ].join(',');
}

/// Đo độ trễ khứ hồi (tạm dừng nhạc), lưu vào lịch sử nếu đo được — bản thu dùng số này để căn
/// giọng khớp nhạc (M7). Trả kết quả và lịch sử mới.
Future<(LatencyResult, List<String>)> measureAndSaveLatency(
  ToneCoreEngine e, {
  required LatencyHistory store,
  required List<String> history,
  DeviceLabel device = const DeviceLabel(),
}) async {
  if (e.playback.playing) e.pause();
  final r = await e.measureLatency();
  if (!r.valid) return (r, history);
  final line = latencyCsv(r, e.metrics, device);
  try {
    return (r, await store.add(line, history));
  } catch (_) {
    // Không ghi được file thì vẫn giữ trong bộ nhớ.
    return (r, [line, ...history].take(LatencyHistory.maxEntries).toList());
  }
}

/// Mức đánh giá độ trễ cho người dùng.
enum LatencyLevel { smooth, slight, high }

/// Đánh giá độ trễ khứ hồi đo được bằng lời thường (docs/m8-plan.md mục 1, Cài đặt → Âm thanh).
/// Ngưỡng theo CLAUDE.md mục 7 / ADR-002: ≤ 35 ms hát nghe lại giọng vẫn liền; tới 60 ms thấy hơi
/// vọng; hơn nữa thì nên tắt nghe lại giọng.
({LatencyLevel level, String title, String advice}) latencyVerdict(double ms) {
  if (ms <= 35) {
    return (
      level: LatencyLevel.smooth,
      title: 'Máy bạn hát mượt',
      advice: 'Nghe lại giọng gần như tức thì. Dùng tai nghe có dây để không bị hú và nghe rõ nhất.',
    );
  }
  if (ms <= 60) {
    return (
      level: LatencyLevel.slight,
      title: 'Hơi trễ, nên dùng tai nghe có dây',
      advice:
          'Giọng nghe lại có thể hơi vọng. Tai nghe có dây (hoặc soundcard) giúp giảm trễ; bản thu '
          'vẫn được căn khớp nhạc.',
    );
  }
  return (
    level: LatencyLevel.high,
    title: 'Trễ nhiều',
    advice:
        'Nếu nghe giọng mình bị vọng khi hát, hãy tắt "Nghe lại giọng". Bản thu vẫn được căn khớp '
        'nhạc theo số đo này.',
  );
}
