// Tìm nhạc lọt vào micro (M8 B6) trên tín hiệu giả: "giọng" (hoà âm, vibrato, ngắt hơi) cộng
// beat (trống, bass, hợp âm) trễ D frame như soundcard bật Loopback.

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/beat_leak.dart';
import 'package:flutter_test/flutter_test.dart';

const _rate = 48000;

/// Beat giả 120 BPM: trống trầm mỗi phách, hi-hat mỗi nửa phách, bass + hợp âm đổi mỗi 2 giây.
Float32List fakeBeat(int frames, {int seed = 1}) {
  final rng = math.Random(seed);
  final out = Float32List(frames);
  const beat = _rate ~/ 2;
  for (var i = 0; i < frames; i++) {
    final t = i / _rate;
    final inBeat = i % beat;
    final inHat = i % (beat ~/ 2);
    final bar = (i ~/ (2 * _rate)) % 4;
    final root = [55.0, 73.4, 82.4, 61.7][bar];
    var v = 0.0;
    v +=
        0.5 *
        math.exp(-inBeat / 2400) *
        math.sin(2 * math.pi * 55 * inBeat / _rate);
    v += 0.15 * math.exp(-inHat / 300) * (rng.nextDouble() * 2 - 1);
    v += 0.12 * math.sin(2 * math.pi * root * t);
    for (final k in [2.0, 2.52, 3.0]) {
      v += 0.04 * math.sin(2 * math.pi * root * 2 * k * t);
    }
    out[i] = v;
  }
  return out;
}

/// "Giọng" giả: nốt đổi mỗi 0,75 s quanh 200–330 Hz, vibrato 5,5 Hz, 8 hoà âm, ngắt hơi 0,25 s mỗi
/// 3 giây, thêm chút tiếng hơi.
Float32List fakeVoice(int frames, {int seed = 2}) {
  final rng = math.Random(seed);
  final out = Float32List(frames);
  const notes = [220.0, 246.9, 261.6, 293.7, 329.6, 293.7, 261.6, 196.0];
  var phase = 0.0;
  for (var i = 0; i < frames; i++) {
    final t = i / _rate;
    final f0 =
        notes[(t / 0.75).floor() % notes.length] *
        (1 + 0.01 * math.sin(2 * math.pi * 5.5 * t));
    phase += 2 * math.pi * f0 / _rate;
    final breath = (t % 3.0) > 2.75;
    var v = 0.0;
    for (var h = 1; h <= 8; h++) {
      v += math.sin(h * phase) / h;
    }
    out[i] = breath
        ? 0.01 * (rng.nextDouble() * 2 - 1)
        : 0.2 * v + 0.005 * (rng.nextDouble() * 2 - 1);
  }
  return out;
}

double rms(Float32List x) {
  var e = 0.0;
  for (final v in x) {
    e += v * v;
  }
  return math.sqrt(e / x.length);
}

/// Giọng khô = giọng + beat trễ [delay] frame, beat lọt nhỏ hơn giọng [belowDb] dB (đảo dấu nếu
/// [invert]).
Float32List mixLeak(
  Float32List voice,
  Float32List beat, {
  required int delay,
  required double belowDb,
  bool invert = false,
}) {
  final g =
      (invert ? -1 : 1) *
      rms(voice) /
      rms(beat) *
      math.pow(10, -belowDb / 20).toDouble();
  final out = Float32List(voice.length);
  for (var i = 0; i < out.length; i++) {
    final n = i - delay;
    out[i] = voice[i] + (n >= 0 ? g * beat[n] : 0);
  }
  return out;
}

BeatLeak? find(Float32List beat, Float32List dry, {required int expected}) {
  Float32List Function(int, int) reader(Float32List x) => (start, count) {
    final out = Float32List(count);
    for (var i = 0; i < count; i++) {
      final n = start + i;
      if (n >= 0 && n < x.length) out[i] = x[n];
    }
    return out;
  };
  return findBeatLeak(
    frames: beat.length,
    sampleRate: _rate,
    expectedLagFrames: expected,
    beat: reader(beat),
    dry: reader(dry),
  );
}

void main() {
  const frames = _rate * 20;
  final beat = fakeBeat(frames);
  final voice = fakeVoice(frames);
  const delay =
      1871; // ≈ 39 ms: soundcard qua USB, không chia hết cho bước hạ mẫu

  test('nhạc lọt nhỏ hơn giọng 10 dB: nhận ra, độ lệch sai ≤ 0,5 ms', () {
    final r = find(
      beat,
      mixLeak(voice, beat, delay: delay, belowDb: 10),
      expected: 1800,
    )!;
    expect(r.leaked, isTrue, reason: '$r');
    expect((r.lagFrames - delay).abs(), lessThanOrEqualTo(24), reason: '$r');
  });

  test('không lẫn nhạc: không báo', () {
    final r = find(beat, voice, expected: 1800)!;
    expect(r.leaked, isFalse, reason: '$r');
    expect(r.correlation, lessThan(0.06), reason: '$r');
  });

  test('đảo dấu, độ lệch dự kiến sai 100 ms: vẫn nhận ra', () {
    final r = find(
      beat,
      mixLeak(voice, beat, delay: delay, belowDb: 10, invert: true),
      expected: delay + 4800,
    )!;
    expect(r.leaked, isTrue, reason: '$r');
    expect((r.lagFrames - delay).abs(), lessThanOrEqualTo(24), reason: '$r');
  });

  test('nhạc lọt rất nhỏ (−30 dB): không báo', () {
    final r = find(
      beat,
      mixLeak(voice, beat, delay: delay, belowDb: 30),
      expected: 1800,
    )!;
    expect(r.leaked, isFalse, reason: '$r');
  });

  test('beat im lặng hoặc bản thu quá ngắn: không kết luận', () {
    expect(find(Float32List(frames), voice, expected: 1800), isNull);
    expect(
      find(beat.sublist(0, _rate), voice.sublist(0, _rate), expected: 1800),
      isNull,
    );
  });

  test(
    'đọc từ file WAV từng đoạn (beat 16-bit stereo, giọng khô float mono)',
    () {
      final dir = Directory.systemTemp.createTempSync('beat_leak');
      addTearDown(() => dir.deleteSync(recursive: true));
      final stereo = Float32List(frames * 2);
      for (var i = 0; i < frames; i++) {
        stereo[2 * i] = stereo[2 * i + 1] = beat[i];
      }
      File(
        '${dir.path}/beat.wav',
      ).writeAsBytesSync(encodeWav16(stereo, sampleRate: _rate, channels: 2));
      final dry = mixLeak(voice, beat, delay: delay, belowDb: 10);
      File('${dir.path}/dry.wav').writeAsBytesSync(encodeWavFloat(dry));
      final r = checkLeakFiles(
        beatPath: '${dir.path}/beat.wav',
        dryPath: '${dir.path}/dry.wav',
        expectedLagFrames: 1800,
      )!;
      expect(r.leaked, isTrue, reason: '$r');
      expect((r.lagFrames - delay).abs(), lessThanOrEqualTo(24), reason: '$r');

      final reader = WavReader.open('${dir.path}/beat.wav');
      addTearDown(reader.close);
      expect(reader.format.frames, frames);
      final part = reader.readMono(frames - 2, 4);
      expect(part[0], closeTo(beat[frames - 2], 1e-4));
      expect(part[2], 0); // ngoài file
      expect(
        checkLeakFiles(
          beatPath: '${dir.path}/khong-co.wav',
          dryPath: '${dir.path}/dry.wav',
          expectedLagFrames: 0,
        ),
        isNull,
      );
    },
  );
}

/// WAV float 32-bit mono (như dry.wav của engine).
Uint8List encodeWavFloat(Float32List x) {
  final b = ByteData(44 + x.length * 4);
  void tag(int at, String s) {
    for (var i = 0; i < 4; i++) {
      b.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  tag(0, 'RIFF');
  b.setUint32(4, 36 + x.length * 4, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  b.setUint32(16, 16, Endian.little);
  b.setUint16(20, 3, Endian.little);
  b.setUint16(22, 1, Endian.little);
  b.setUint32(24, _rate, Endian.little);
  b.setUint32(28, _rate * 4, Endian.little);
  b.setUint16(32, 4, Endian.little);
  b.setUint16(34, 32, Endian.little);
  tag(36, 'data');
  b.setUint32(40, x.length * 4, Endian.little);
  for (var i = 0; i < x.length; i++) {
    b.setFloat32(44 + 4 * i, x[i], Endian.little);
  }
  return b.buffer.asUint8List();
}
