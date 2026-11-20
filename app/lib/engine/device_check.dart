import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show visibleForTesting;
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/wav_io.dart';

/// "Kiểm tra máy" (M9 B4, docs/m9-plan.md): đo máy có đủ khoẻ chạy chuỗi giọng không — không cần
/// mạng, không thu âm, không cần máy tính. Một engine phụ không có thiết bị âm thanh xử lý giọng
/// giả (giai điệu La thứ, rung, hơi lệch tone để chỉnh giọng phải làm việc, có khoảng lặng) + beat
/// giả qua cả chuỗi (chỉnh giọng Ca sĩ, Bolero, limiter), nhanh hết mức, ở isolate riêng; đo thời gian
/// từng khối 256 mẫu (chỉ lời gọi engine, bộ đệm native dùng lại).
///
/// Không đo được xrun do I/O (luồng âm thanh của hệ điều hành bị trễ) — phần đó đo bằng hát thật.
class DeviceCheckResult {
  const DeviceCheckResult({
    required this.audio,
    required this.blocks,
    required this.blockFrames,
    required this.sampleRate,
    required this.meanUs,
    required this.p99Us,
    required this.maxUs,
    required this.elapsed,
    required this.badSamples,
    required this.peak,
  });

  final Duration audio;
  final int blocks;
  final int blockFrames;
  final int sampleRate;
  final double meanUs;
  final double p99Us;
  final double maxUs;

  /// Thời gian chạy thật (cả phần tạo tín hiệu, chép bộ đệm).
  final Duration elapsed;

  /// Mẫu NaN / Inf / vượt 0,99 (limiter đầu ra: trần 0,98, cho phép vượt < 1%).
  final int badSamples;
  final double peak;

  double get blockUs => 1e6 * blockFrames / sampleRate;

  /// Tỉ lệ một nhân (0..1) — chỉ tiêu mục 7: ≤ 0,25.
  double get meanLoad => meanUs / blockUs;
  double get p99Load => p99Us / blockUs;

  /// Engine nhanh hơn thời gian thực bao nhiêu lần (theo thời gian trung bình mỗi khối).
  double get realtimeFactor => meanUs <= 0 ? double.infinity : blockUs / meanUs;

  /// Theo CPU trung bình — đúng định nghĩa "% một nhân" của mục 7. p99 / cao nhất chỉ để tham
  /// khảo: luồng chạy thử không có ưu tiên real-time như luồng âm thanh thật nên bị hệ điều hành
  /// chen ngang (Mac M1: trung bình 2,0%, p99 11%, trong khi test C++ cùng máy p99 2,1%).
  DeviceCheckLevel get level {
    if (badSamples > 0) return DeviceCheckLevel.broken;
    if (meanLoad <= 0.25) return DeviceCheckLevel.good;
    if (meanLoad <= 0.5) return DeviceCheckLevel.weak;
    return DeviceCheckLevel.poor;
  }
}

enum DeviceCheckLevel { good, weak, poor, broken }

/// Đang chạy; [cancel] để dừng sớm (kết quả `null`).
class DeviceCheckRun {
  DeviceCheckRun._(this.result, this._cancel);

  /// Cho test màn hình (bộ chạy giả).
  @visibleForTesting
  DeviceCheckRun.test(this.result, this._cancel);

  final Future<DeviceCheckResult?> result;
  final void Function() _cancel;

  void cancel() => _cancel();
}

/// Chạy kiểm tra [audio] âm thanh. [tempDir]: chỗ ghi beat giả (xoá khi xong). [onProgress] 0..1.
DeviceCheckRun startDeviceCheck({
  required Duration audio,
  required String tempDir,
  void Function(double progress)? onProgress,
}) {
  final fromIsolate = ReceivePort();
  final done = Completer<DeviceCheckResult?>();
  SendPort? toIsolate;
  var cancelled = false;
  fromIsolate.listen((m) {
    switch (m) {
      case SendPort p:
        toIsolate = p;
        if (cancelled) p.send('cancel');
      case double p:
        onProgress?.call(p);
      case DeviceCheckResult r:
        fromIsolate.close();
        if (!done.isCompleted) done.complete(r);
      case null:
        fromIsolate.close();
        if (!done.isCompleted) done.complete(null);
      case String error:
        fromIsolate.close();
        if (!done.isCompleted) done.completeError(StateError(error));
    }
  });
  unawaited(
    Isolate.spawn(_main, (
      fromIsolate.sendPort,
      audio.inMilliseconds,
      tempDir,
    )).catchError((Object e) {
      fromIsolate.close();
      if (!done.isCompleted) done.completeError(e);
      return Isolate.current; // không dùng
    }),
  );
  return DeviceCheckRun._(done.future, () {
    cancelled = true;
    toIsolate?.send('cancel');
  });
}

const _rate = 48000;
const _block = 256;

Future<void> _main((SendPort, int, String) args) async {
  final (out, audioMs, tempDir) = args;
  final inbox = ReceivePort();
  out.send(inbox.sendPort);
  var cancelled = false;
  inbox.listen((m) {
    if (m == 'cancel') cancelled = true;
  });
  final beatFile = File('$tempDir/kiem-tra-may-beat.wav');
  ToneCoreEngine? e;
  OfflineBlock? block;
  try {
    final total = Stopwatch()..start();
    final voice = synthCheckVoice(_rate);
    await beatFile.writeAsBytes(
      encodeWav16(synthCheckBeat(_rate), sampleRate: _rate, channels: 2),
    );
    final engine = e = ToneCoreEngine.create(maxBlockFrames: _block);
    engine.start();
    engine.key = const MusicKey(9, MusicScale.naturalMinor);
    engine.setParam(TcNode.pitch, TcParam.enabled, 1);
    engine.applyCorrectionPreset(CorrectionPreset.singer);
    engine.monitorEnabled = true;
    engine.setParam(TcNode.gate, TcParam.enabled, 1);
    engine.applyFxPreset(ToneCore.genrePreset(Genre.bolero));
    await engine.loadBeatAndWait(beatFile.path);
    await beatFile.delete(); // engine đã giải mã beat vào RAM
    engine.play();

    final b = block = engine.offlineBlock(_block);
    final blocks = math.max(1, audioMs * _rate ~/ 1000 ~/ _block);
    final micros = Int32List(blocks);
    final sw = Stopwatch();
    var off = 0;
    var bad = 0;
    var peak = 0.0;
    final progressStep = math.max(1, blocks ~/ 100);
    for (var i = 0; i < blocks; i++) {
      if (off + _block > voice.length) off = 0;
      b.input.setRange(0, _block, voice, off);
      off += _block;
      sw
        ..reset()
        ..start();
      b.process();
      sw.stop();
      micros[i] = sw.elapsedMicroseconds;
      for (final v in b.output) {
        final a = v.abs();
        if (!a.isFinite || a > 0.99) bad++;
        if (a > peak) peak = a;
      }
      if (i % progressStep == 0) {
        out.send(i / blocks);
        // Nhường để nhận lệnh huỷ và sự kiện engine; hết bài thì phát lại.
        await Future<void>.delayed(Duration.zero);
        if (cancelled) {
          out.send(null);
          return;
        }
        if (!engine.playback.playing) {
          engine.seek(Duration.zero);
          engine.play();
        }
      }
    }
    final sorted = Int32List.fromList(micros)..sort();
    var sum = 0;
    for (final m in micros) {
      sum += m;
    }
    out.send(
      DeviceCheckResult(
        audio: Duration(milliseconds: audioMs),
        blocks: blocks,
        blockFrames: _block,
        sampleRate: _rate,
        meanUs: sum / blocks,
        p99Us: sorted[(blocks * 99) ~/ 100].toDouble(),
        maxUs: sorted.last.toDouble(),
        elapsed: total.elapsed,
        badSamples: bad,
        peak: peak,
      ),
    );
  } catch (err) {
    out.send('$err');
  } finally {
    block?.dispose();
    e?.dispose();
    inbox.close();
    try {
      if (beatFile.existsSync()) beatFile.deleteSync();
    } catch (_) {}
  }
}

/// Giọng giả ~16 s (mono): giai điệu La thứ, 10 hoạ âm, rung 5,5 Hz sau đầu nốt, vài nốt lệch
/// +35 cent (chỉnh giọng phải kéo về), khoảng lặng có hơi thở nhỏ.
Float32List synthCheckVoice(int rate) {
  const notes = [
    57,
    60,
    62,
    64,
    65,
    64,
    62,
    60,
    59,
    57,
    64,
    69,
    67,
    65,
    64,
    62,
  ];
  const lengths = [
    0.8,
    0.5,
    0.5,
    1.2,
    0.5,
    0.5,
    0.8,
    0.5,
    0.5,
    1.5,
    0.6,
    1.2,
    0.5,
    0.5,
    0.8,
    1.6,
  ];
  final out = <double>[];
  final rnd = math.Random(7);
  var phase = 0.0;
  for (var n = 0; n < notes.length; n++) {
    final detune = n % 3 == 1 ? 0.35 : 0.0; // nửa cung
    final f0 = 440.0 * math.pow(2, (notes[n] + detune - 69) / 12);
    final len = (lengths[n] * rate).round();
    for (var i = 0; i < len; i++) {
      final t = i / rate;
      final vib = t > 0.25
          ? 0.4 * math.sin(2 * math.pi * 5.5 * t)
          : 0.0; // ±40 cent
      final f = f0 * math.pow(2, vib / 12);
      phase += 2 * math.pi * f / rate;
      var v = 0.0;
      for (var k = 1; k <= 10; k++) {
        v += math.sin(k * phase) / k;
      }
      final env = math.min(
        1.0,
        math.min(i / (0.03 * rate), (len - i) / (0.05 * rate)),
      );
      out.add(0.18 * env * v);
    }
    // Lặng 0,15 s, hơi thở −50 dB.
    for (var i = 0; i < (0.15 * rate).round(); i++) {
      out.add(0.003 * (rnd.nextDouble() * 2 - 1));
    }
  }
  return Float32List.fromList(out);
}

/// Beat giả 12 s (stereo): hợp âm Am – F – C – G, nhịp mạnh nhẹ.
Float32List synthCheckBeat(int rate) {
  const chords = [
    [57, 60, 64],
    [53, 57, 60],
    [48, 52, 55],
    [55, 59, 62],
  ];
  final frames = 12 * rate;
  final out = Float32List(2 * frames);
  for (var i = 0; i < frames; i++) {
    final t = i / rate;
    final chord = chords[(t ~/ 3) % 4];
    var v = 0.0;
    for (final m in chord) {
      v += math.sin(2 * math.pi * 440 * math.pow(2, (m - 69) / 12) * t);
    }
    final pulse = 0.6 + 0.4 * math.exp(-8 * (t % 0.5));
    final s = 0.12 * pulse * v;
    out[2 * i] = s;
    out[2 * i + 1] = s;
  }
  return out;
}

String _pct(double load) =>
    '${(load * 100).toStringAsFixed(1).replaceAll('.', ',')}%';

/// "1,9% một nhân (lúc nặng nhất 3,1%)" — câu cho người dùng.
String deviceCheckLoadLine(DeviceCheckResult r) =>
    '${_pct(r.meanLoad)} một nhân (lúc nặng nhất ${_pct(r.p99Load)})';

/// Câu đánh giá cho người dùng.
String deviceCheckVerdict(DeviceCheckLevel l) => switch (l) {
  DeviceCheckLevel.good => 'Máy đủ khoẻ — chỉnh giọng chạy mượt.',
  DeviceCheckLevel.weak =>
    'Máy hơi yếu — có thể rè khi máy nóng hoặc mở nhiều app.',
  DeviceCheckLevel.poor =>
    'Máy yếu — nên dùng mức Tự nhiên hoặc tắt chỉnh giọng.',
  DeviceCheckLevel.broken =>
    'Có lỗi khi xử lý âm thanh — bấm "Gửi kết quả" cho nhóm phát triển.',
};

/// Báo cáo chữ để gửi nhóm phát triển (bảng chia sẻ) — dùng cho đợt thử 13 máy (M9b / M9c).
String deviceCheckReport(
  DeviceCheckResult r, {
  required DateTime at,
  required String device,
  required String appVersion,
  String? engineVersion,
  List<String> latency = const [],
  int? xruns,
  String? health,
}) {
  String two(int v) => v.toString().padLeft(2, '0');
  final when =
      '${two(at.day)}/${two(at.month)}/${at.year} ${two(at.hour)}:${two(at.minute)}';
  final minutes = r.audio.inSeconds >= 60;
  final audio = minutes
      ? '${r.audio.inMinutes} phút'
      : '${r.audio.inSeconds} giây';
  return [
    'ChuẩnTone – Kiểm tra máy',
    'Thời điểm: $when',
    'Máy: $device · app $appVersion${engineVersion == null ? '' : ' · engine $engineVersion'}',
    'Kiểm tra: $audio âm thanh qua cả chuỗi giọng (chỉnh giọng Ca sĩ, Bolero, nghe lại, beat), '
        'khối ${r.blockFrames} mẫu',
    'CPU trung bình ${_pct(r.meanLoad)} một nhân · p99 ${_pct(r.p99Load)} · '
        'cao nhất ${_pct(r.maxUs / r.blockUs)} · nhanh hơn thời gian thực '
        '${r.realtimeFactor.round()} lần',
    'Mẫu lỗi (NaN / vượt trần): ${r.badSamples} · đỉnh ${r.peak.toStringAsFixed(3).replaceAll('.', ',')}',
    'Đánh giá: ${deviceCheckVerdict(r.level)} (chỉ tiêu mục 7: trung bình ≤ 25%)',
    'Độ trễ đã đo: ${latency.isEmpty ? 'chưa đo' : latency.join('; ')}',
    if (xruns != null) 'Xrun phiên hiện tại: $xruns',
    if (health != null) 'Sức khoẻ app: $health',
  ].join('\n');
}
