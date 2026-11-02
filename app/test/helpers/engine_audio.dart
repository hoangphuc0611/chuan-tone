import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:chuantone/data/wav_io.dart';
import 'package:flutter_test/flutter_test.dart';

/// Thư viện engine cho máy host (`TONECORE_LIB`, xem docs/build.md). Test cần engine thật (backend
/// NULL, xử lý bằng `processOffline`) bỏ qua khi thiếu — trừ trên CI (phải có).
String? engineSkipReason() {
  final lib = Platform.environment['TONECORE_LIB'];
  if (lib != null && lib.isNotEmpty) return null;
  if (Platform.environment['CI'] == 'true') {
    fail(
      'CI phải đặt TONECORE_LIB cho test app (.github/workflows/flutter.yml)',
    );
  }
  return 'cần TONECORE_LIB=../engine/build/host/libtonecore.dylib';
}

/// Vòng hoà âm Rê trưởng I–IV–V–I (2 s mỗi hợp âm, 2 vòng = 16 s) có nốt bass, cộng tiếng gõ mỗi
/// 0,5 s (120 BPM). WAV 16-bit stereo 44,1 kHz — engine dò ra Rê trưởng, ~120 BPM.
void writeCadenceWav(String path) {
  const sampleRate = 44100;
  const frames = sampleRate * 16;
  const chords = [
    [50, 62, 66, 69], // D
    [43, 62, 67, 71], // G
    [45, 61, 64, 69], // A
    [50, 62, 66, 69], // D
  ];
  final mix = Float64List(frames);
  double hz(int midi) => 440.0 * math.pow(2, (midi - 69) / 12);
  for (var c = 0; c < 8; c++) {
    final start = c * 2 * sampleRate;
    for (final m in chords[c % 4]) {
      for (var h = 1; h <= 4; h++) {
        final f = hz(m) * h;
        for (var i = 0; i < 2 * sampleRate; i++) {
          final env =
              math.min(1.0, i / 441) *
              math.min(1.0, (2 * sampleRate - i) / 2205);
          mix[start + i] +=
              0.05 * env * math.sin(2 * math.pi * f * i / sampleRate) / h;
        }
      }
    }
  }
  final rng = math.Random(7);
  for (var k = 0; k < 32; k++) {
    final start = k * sampleRate ~/ 2;
    for (var i = 0; i < 1300 && start + i < frames; i++) {
      mix[start + i] += 0.4 * math.exp(-i / 176.0) * (rng.nextDouble() * 2 - 1);
    }
  }
  final stereo = Float32List(frames * 2);
  for (var i = 0; i < frames; i++) {
    stereo[2 * i] = stereo[2 * i + 1] = mix[i];
  }
  File(
    path,
  ).writeAsBytesSync(encodeWav16(stereo, sampleRate: sampleRate, channels: 2));
}

/// Khối giọng giả cho micro: sin [hz] mono, [frames] mẫu ở 48 kHz.
Float32List sineBlock(int frames, {double hz = 220, int offset = 0}) {
  final out = Float32List(frames);
  for (var i = 0; i < frames; i++) {
    out[i] = 0.3 * math.sin(2 * math.pi * hz * (offset + i) / 48000);
  }
  return out;
}
