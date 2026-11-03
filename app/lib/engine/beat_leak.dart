import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import '../data/take_store.dart';
import '../data/wav_io.dart';
import 'take_alignment.dart';

/// Ngưỡng hệ số tương quan để coi là nhạc lọt vào micro. r² ≈ phần năng lượng giọng khô là nhạc:
/// 0,15 ≈ nhạc lọt nhỏ hơn giọng ~17 dB (đo trên tín hiệu giả của test; không lẫn nhạc r ≈ 0,03) —
/// nhỏ hơn nữa thì trộn vào bản xuất khó nghe ra.
const beatLeakThreshold = 0.15;

/// Kết quả tìm nhạc lọt vào micro trong một bản thu.
class BeatLeak {
  const BeatLeak({
    required this.correlation,
    required this.lagFrames,
    required this.sampleRate,
  });

  /// |r| lớn nhất (0..1) giữa giọng khô và beat đã thu.
  final double correlation;

  /// Độ lệch (frame) của đỉnh: nhạc lọt chậm hơn beat bao nhiêu.
  final int lagFrames;
  final int sampleRate;

  bool get leaked => correlation >= beatLeakThreshold;
  double get lagMs => lagFrames * 1000 / sampleRate;

  @override
  String toString() =>
      'BeatLeak(r ${correlation.toStringAsFixed(3)}, ${lagMs.toStringAsFixed(2)} ms)';
}

/// Tìm nhạc lọt vào micro (M8 B6: soundcard bật Loopback trộn tiếng điện thoại vào lại micro).
///
/// Nhạc lọt = beat đã phát, chậm đúng một vòng ra → vào ([expectedLagFrames] ≈ độ trễ khứ hồi +
/// limiter nhìn trước), nhân một hệ số (có thể đảo dấu). Giọng hát gần như không tương quan với beat
/// ở một độ lệch cố định, nên hệ số tương quan chuẩn hoá
///   r(L) = Σ d[n+L]·b[n] / √(Σ d[n+L]² · Σ b[n]²)
/// (d = giọng khô, b = beat) có đỉnh rõ ở độ lệch của đường lọt, và r² ≈ phần năng lượng giọng khô
/// là nhạc.
///
/// Cho nhanh: lấy tối đa [windows] đoạn dài [windowMs] có beat to nhất, hạ mẫu về ~8 kHz (trung
/// bình từng nhóm mẫu — cũng là lọc thông thấp), dò mọi độ lệch trong [expectedLagFrames] ±
/// [searchMs] theo bước hạ mẫu (0,125 ms; đỉnh tù nên độ lệch chỉ để tham khảo). Không làm phẳng
/// phổ: phần trầm (trống, bass) là nơi giọng yếu nên nhạc lọt nổi rõ nhất; làm phẳng ở 8 kHz lại
/// đẩy dải 2–4 kHz, đúng chỗ giọng mạnh nhất, nhạc lọt bị giọng át. [beat] / [dry] trả [count]
/// frame mono từ frame [start] (ngoài bản thu = 0). Bản thu quá ngắn, hoặc beat nhỏ hơn −50 dBFS
/// ở mọi đoạn → null.
BeatLeak? findBeatLeak({
  required int frames,
  required int sampleRate,
  required int expectedLagFrames,
  required Float32List Function(int start, int count) beat,
  required Float32List Function(int start, int count) dry,
  int windows = 4,
  int windowMs = 2000,
  int searchMs = 150,
}) {
  final factor = math.max(1, sampleRate ~/ 8000);
  final win = windowMs * sampleRate ~/ 1000 ~/ factor * factor;
  final search = searchMs * sampleRate ~/ 1000;
  final lagMin = math.max(0, expectedLagFrames - search) ~/ factor * factor;
  final lagMax = (expectedLagFrames + search) ~/ factor * factor;
  final usable = frames - lagMax - win;
  if (win == 0 || usable < 0) return null;

  // Chọn đoạn: 8 vị trí rải đều, giữ các đoạn beat to nhất.
  const candidates = 8;
  const minRms = 0.00316; // −50 dBFS
  final scored = <(double, int, Float32List)>[];
  final seen = <int>{};
  for (var k = 0; k < candidates; k++) {
    final start = usable * k ~/ (candidates - 1);
    if (!seen.add(start)) continue;
    final b = beat(start, win);
    var e = 0.0;
    for (final v in b) {
      e += v * v;
    }
    final rms = math.sqrt(e / win);
    if (rms >= minRms) scored.add((rms, start, b));
  }
  if (scored.isEmpty) return null;
  scored.sort((a, b) => b.$1.compareTo(a.$1));
  final picked = [
    for (final (_, start, b) in scored.take(windows))
      (
        beat: _decimate(b, factor, 0, win),
        dry: _decimate(
          dry(start + lagMin, win + lagMax - lagMin),
          factor,
          0,
          win + lagMax - lagMin,
        ),
      ),
  ];

  // Trượt beat trên giọng khô theo từng bước hạ mẫu, cộng dồn qua các đoạn.
  final m = win ~/ factor;
  final lags = (lagMax - lagMin) ~/ factor + 1;
  final num = Float64List(lags);
  final dryEnergy = Float64List(lags);
  var beatEnergy = 0.0;
  for (final w in picked) {
    final b = w.beat;
    final d = w.dry;
    for (var i = 0; i < m; i++) {
      beatEnergy += b[i] * b[i];
    }
    // Tổng d² trên cửa sổ trượt (cộng dồn).
    final prefix = Float64List(d.length + 1);
    for (var i = 0; i < d.length; i++) {
      prefix[i + 1] = prefix[i] + d[i] * d[i];
    }
    for (var l = 0; l < lags && l + m <= d.length; l++) {
      var acc = 0.0;
      for (var i = 0; i < m; i++) {
        acc += b[i] * d[i + l];
      }
      num[l] += acc;
      dryEnergy[l] += prefix[l + m] - prefix[l];
    }
  }
  var best = 0;
  var bestR = -1.0;
  for (var l = 0; l < lags; l++) {
    final den = math.sqrt(beatEnergy * dryEnergy[l]);
    final r = den > 0 ? (num[l] / den).abs() : 0.0;
    if (r > bestR) {
      bestR = r;
      best = l;
    }
  }

  return BeatLeak(
    correlation: bestR,
    lagFrames: lagMin + best * factor,
    sampleRate: sampleRate,
  );
}

/// [count] frame của [x] từ [offset], trung bình từng nhóm [factor] mẫu.
Float32List _decimate(Float32List x, int factor, int offset, int count) {
  final n = count ~/ factor;
  final out = Float32List(n);
  for (var i = 0; i < n; i++) {
    var s = 0.0;
    final at = offset + i * factor;
    for (var k = 0; k < factor; k++) {
      s += x[at + k];
    }
    out[i] = s / factor;
  }
  return out;
}

/// Tìm nhạc lọt trong file bản thu (đọc từng đoạn, không nạp cả bài). File hỏng / thiếu → null.
BeatLeak? checkLeakFiles({
  required String beatPath,
  required String dryPath,
  required int expectedLagFrames,
}) {
  WavReader? b;
  WavReader? d;
  try {
    b = WavReader.open(beatPath);
    d = WavReader.open(dryPath);
    return findBeatLeak(
      frames: math.min(b.format.frames, d.format.frames),
      sampleRate: d.format.sampleRate,
      expectedLagFrames: expectedLagFrames,
      beat: b.readMono,
      dry: d.readMono,
    );
  } on FileSystemException {
    return null;
  } on FormatException {
    return null;
  } finally {
    b?.close();
    d?.close();
  }
}

/// Tìm nhạc lọt trong bản thu [take] ở isolate riêng (không chặn giao diện). Độ lệch dự kiến =
/// độ trễ khứ hồi + limiter nhìn trước (không cộng phần người dùng chỉnh "Giọng sớm / muộn").
Future<BeatLeak?> checkTakeLeak(Take take) {
  final i = take.info;
  final beatPath = take.beatPath;
  final dryPath = take.dryPath;
  final lag = voiceOffsetFrames(
    roundTripMs: i.roundTripMs,
    sampleRate: i.sampleRate,
    lookaheadFrames: i.outputLookaheadFrames,
  );
  return Isolate.run(
    () => checkLeakFiles(
      beatPath: beatPath,
      dryPath: dryPath,
      expectedLagFrames: lag,
    ),
  );
}
