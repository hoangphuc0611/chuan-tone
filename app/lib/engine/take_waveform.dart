import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import '../data/take_store.dart';
import '../data/wav_io.dart';

/// Hình sóng của bản thu cho màn Nghe lại (giao diện mới, docs/ui/redesign-plan.md): [bins] cột, mỗi
/// cột là độ to (RMS) của giọng khô + nhạc trong đoạn đó, chuẩn hoá về 0..1 theo cột to nhất. Đọc
/// từng khúc file WAV ở isolate riêng (bản thu 4 phút ≈ 23 triệu mẫu).
Future<Float32List> takeWaveform(Take take, {int bins = 96}) {
  final dry = take.dryPath;
  final beat = take.beatPath;
  return Isolate.run(() => computeWaveform(dry, beat, bins: bins));
}

/// Như [takeWaveform], chạy ngay trên luồng đang gọi (test, isolate).
Float32List computeWaveform(String dryPath, String beatPath, {int bins = 96}) {
  final dry = WavReader.open(dryPath);
  final beat = WavReader.open(beatPath);
  try {
    final frames = math.max(dry.format.frames, beat.format.frames);
    final out = Float32List(bins);
    if (frames == 0 || bins == 0) return out;
    final sums = Float64List(bins);
    final counts = Int32List(bins);
    const chunk = 65536;
    for (var start = 0; start < frames; start += chunk) {
      final n = math.min(chunk, frames - start);
      final d = dry.readMono(start, n);
      final b = beat.readMono(start, n);
      for (var i = 0; i < n; i++) {
        final bin = (start + i) * bins ~/ frames;
        // Tổng năng lượng hai nguồn (không trộn mẫu: lệch độ trễ giữa giọng và nhạc không làm
        // sóng méo đi).
        sums[bin] += d[i] * d[i] + b[i] * b[i];
        counts[bin]++;
      }
    }
    var peak = 0.0;
    for (var i = 0; i < bins; i++) {
      final rms = counts[i] == 0 ? 0.0 : math.sqrt(sums[i] / counts[i]);
      out[i] = rms;
      if (rms > peak) peak = rms;
    }
    if (peak > 0) {
      for (var i = 0; i < bins; i++) {
        out[i] = out[i] / peak;
      }
    }
    return out;
  } finally {
    dry.close();
    beat.close();
  }
}
