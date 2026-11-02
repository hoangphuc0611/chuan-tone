import 'dart:math' as math;
import 'dart:typed_data';

/// Beat thử khớp tiếng: tiếng "tách" ngắn (3 ms, 2 kHz, cửa sổ Hann) mỗi [interval], bắt đầu
/// sau [lead]. Mono.
Float32List clickTrack({
  int sampleRate = 48000,
  Duration length = const Duration(seconds: 8),
  Duration interval = const Duration(milliseconds: 500),
  Duration lead = const Duration(milliseconds: 250),
}) {
  final n = length.inMicroseconds * sampleRate ~/ 1000000;
  final step = interval.inMicroseconds * sampleRate ~/ 1000000;
  final burst = 3 * sampleRate ~/ 1000;
  final out = Float32List(n);
  for (
    var at = lead.inMicroseconds * sampleRate ~/ 1000000;
    at + burst < n;
    at += step
  ) {
    for (var i = 0; i < burst; i++) {
      final w = 0.5 - 0.5 * math.cos(2 * math.pi * i / burst);
      out[at + i] = 0.8 * w * math.sin(2 * math.pi * 2000 * i / sampleRate);
    }
  }
  return out;
}

/// Vị trí (frame) các tiếng "tách": lần đầu |x| vượt [relThreshold] × đỉnh sau ít nhất
/// [refractory] im. Tín hiệu quá nhỏ (đỉnh < 0,005) → rỗng.
List<int> detectOnsets(
  Float32List x, {
  required int sampleRate,
  double relThreshold = 0.3,
  Duration refractory = const Duration(milliseconds: 250),
}) {
  var peak = 0.0;
  for (final v in x) {
    peak = math.max(peak, v.abs());
  }
  if (peak < 0.005) return const [];
  final thr = relThreshold * peak;
  final gap = refractory.inMicroseconds * sampleRate ~/ 1000000;
  final onsets = <int>[];
  var last = -gap;
  for (var i = 0; i < x.length; i++) {
    if (x[i].abs() >= thr && i - last >= gap) {
      onsets.add(i);
    }
    if (x[i].abs() >= thr) last = i;
  }
  return onsets;
}

/// Kết quả kiểm tra khớp tiếng trên một bản thu.
class SyncMeasurement {
  const SyncMeasurement({
    required this.lagFrames,
    required this.spreadFrames,
    required this.pairs,
    required this.sampleRate,
  });

  /// Giọng khô chậm hơn beat bao nhiêu frame (trung vị).
  final int lagFrames;

  /// Khoảng 10%–90% của các độ lệch.
  final int spreadFrames;

  /// Số tiếng "tách" ghép được.
  final int pairs;
  final int sampleRate;

  double get lagMs => lagFrames * 1000 / sampleRate;
  double get spreadMs => spreadFrames * 1000 / sampleRate;
}

/// So tiếng "tách" trong beat đã thu với tiếng micro thu lại (loa → micro): mỗi tiếng beat ghép
/// với tiếng micro đầu tiên trong [maxLag] sau nó. Ít hơn 4 cặp → null.
SyncMeasurement? measureSync({
  required Float32List beat,
  required Float32List dry,
  required int sampleRate,
  Duration maxLag = const Duration(milliseconds: 400),
}) {
  final b = detectOnsets(beat, sampleRate: sampleRate);
  final d = detectOnsets(dry, sampleRate: sampleRate);
  final window = maxLag.inMicroseconds * sampleRate ~/ 1000000;
  final lags = <int>[];
  var j = 0;
  for (final t in b) {
    while (j < d.length && d[j] < t) {
      j++;
    }
    if (j < d.length && d[j] - t <= window) lags.add(d[j] - t);
  }
  if (lags.length < 4) return null;
  lags.sort();
  int q(double p) => lags[((lags.length - 1) * p).round()];
  return SyncMeasurement(
    lagFrames: q(0.5),
    spreadFrames: q(0.9) - q(0.1),
    pairs: lags.length,
    sampleRate: sampleRate,
  );
}
