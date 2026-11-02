import 'package:tonecore_flutter/tonecore_flutter.dart';

/// Một lần đo độ trễ khứ hồi đã lưu (dòng CSV của màn Đo độ trễ).
class LatencyEntry {
  const LatencyEntry({
    required this.route,
    required this.measuredMs,
    required this.reportedMs,
  });

  final AudioRoute route;
  final double measuredMs;

  /// Độ trễ vào + ra hệ điều hành báo lúc đo.
  final double reportedMs;
}

/// Tách một dòng CSV (trường có thể nằm trong ngoặc kép).
List<String> splitCsvLine(String line) {
  final out = <String>[];
  final cur = StringBuffer();
  var quoted = false;
  for (var i = 0; i < line.length; i++) {
    final ch = line[i];
    if (ch == '"') {
      quoted = !quoted;
    } else if (ch == ',' && !quoted) {
      out.add(cur.toString());
      cur.clear();
    } else {
      cur.write(ch);
    }
  }
  out.add(cur.toString());
  return out;
}

/// Đọc lịch sử đo (mới nhất trước, theo `LatencyPage.csvHeader`: route ở cột 4, measured_ms cột
/// 9, reported_ms cột 10). Dòng hỏng bị bỏ.
List<LatencyEntry> parseLatencyHistory(List<String> lines) => [
  for (final line in lines)
    if (splitCsvLine(line) case final p when p.length >= 11)
      if ((
            AudioRoute.values.where((r) => r.name == p[4]).firstOrNull,
            double.tryParse(p[9]),
            double.tryParse(p[10]),
          )
          case (final route?, final measured?, final reported?))
        LatencyEntry(route: route, measuredMs: measured, reportedMs: reported),
];

/// Độ trễ khứ hồi lấy từ đâu.
enum LatencySource {
  /// Đo được (`tc_measure_latency`) cho đúng đường ra này.
  measured,

  /// Hệ điều hành báo + phần chênh đo được ở loa ngoài.
  corrected,

  /// Chỉ có số hệ điều hành báo (Bluetooth / chưa đo lần nào).
  reported,
}

class RoundTrip {
  const RoundTrip(this.ms, this.source);

  final double ms;
  final LatencySource source;
}

/// Chọn độ trễ khứ hồi để bù cho bản thu (docs/adr/007-recording-sync.md, kế hoạch M7 điểm 5).
/// [reportedMs]: độ trễ vào + ra hệ điều hành báo lúc thu; [history]: mới nhất trước.
RoundTrip roundTripFor({
  required AudioRoute route,
  required double reportedMs,
  required List<LatencyEntry> history,
}) {
  if (route != AudioRoute.bluetooth) {
    final same = history.where((e) => e.route == route).firstOrNull;
    if (same != null) return RoundTrip(same.measuredMs, LatencySource.measured);
    final speaker = history
        .where((e) => e.route == AudioRoute.speaker)
        .firstOrNull;
    if (speaker != null) {
      return RoundTrip(
        reportedMs + (speaker.measuredMs - speaker.reportedMs),
        LatencySource.corrected,
      );
    }
  }
  return RoundTrip(reportedMs, LatencySource.reported);
}

/// Số frame giọng khô phải dịch sớm để khớp beat: độ trễ khứ hồi + limiter đầu ra nhìn trước.
int voiceOffsetFrames({
  required double roundTripMs,
  required int sampleRate,
  required int lookaheadFrames,
}) => (roundTripMs * sampleRate / 1000).round() + lookaheadFrames;
