import 'dart:io';
import 'dart:typed_data';

/// Âm thanh đọc từ file WAV: float interleaved (−1..1).
class WavAudio {
  const WavAudio({
    required this.sampleRate,
    required this.channels,
    required this.samples,
  });

  final int sampleRate;
  final int channels;
  final Float32List samples;

  int get frames => channels == 0 ? 0 : samples.length ~/ channels;

  /// Kênh [c] tách riêng.
  Float32List channel(int c) {
    final out = Float32List(frames);
    for (var i = 0; i < out.length; i++) {
      out[i] = samples[i * channels + c];
    }
    return out;
  }
}

/// Đọc WAV PCM 16-bit hoặc float 32-bit (file bản thu của engine, docs/adr/007-recording-sync.md).
/// Không đúng định dạng → [FormatException].
WavAudio readWavFile(String path) => parseWav(File(path).readAsBytesSync());

WavAudio parseWav(Uint8List bytes) {
  final d = ByteData.sublistView(bytes);
  String tag(int at) => String.fromCharCodes(bytes.sublist(at, at + 4));
  if (bytes.length < 12 || tag(0) != 'RIFF' || tag(8) != 'WAVE') {
    throw const FormatException('không phải WAV');
  }
  int? format;
  var channels = 0;
  var rate = 0;
  var bits = 0;
  var pos = 12;
  while (pos + 8 <= bytes.length) {
    final id = tag(pos);
    final size = d.getUint32(pos + 4, Endian.little);
    final body = pos + 8;
    final avail = (bytes.length - body).clamp(0, size);
    if (id == 'fmt ' && avail >= 16) {
      format = d.getUint16(body, Endian.little);
      channels = d.getUint16(body + 2, Endian.little);
      rate = d.getUint32(body + 4, Endian.little);
      bits = d.getUint16(body + 14, Endian.little);
      if (format == 0xFFFE && avail >= 26) {
        format = d.getUint16(body + 24, Endian.little);
      }
    } else if (id == 'data') {
      if (format == null || channels == 0) break;
      final isFloat = format == 3 && bits == 32;
      final isPcm16 = format == 1 && bits == 16;
      if (!isFloat && !isPcm16) {
        throw FormatException('WAV $format/$bits bit chưa hỗ trợ');
      }
      final bytesPer = bits ~/ 8;
      final count = avail ~/ bytesPer ~/ channels * channels;
      final out = Float32List(count);
      for (var i = 0; i < count; i++) {
        final at = body + i * bytesPer;
        out[i] = isFloat
            ? d.getFloat32(at, Endian.little)
            : d.getInt16(at, Endian.little) / 32768.0;
      }
      return WavAudio(sampleRate: rate, channels: channels, samples: out);
    }
    pos = body + size + (size & 1);
  }
  throw const FormatException('WAV thiếu fmt / data');
}

/// WAV PCM 16-bit từ float interleaved (kẹp ±1).
Uint8List encodeWav16(
  Float32List interleaved, {
  required int sampleRate,
  required int channels,
}) {
  final dataBytes = interleaved.length * 2;
  final b = ByteData(44 + dataBytes);
  void tag(int at, String s) {
    for (var i = 0; i < 4; i++) {
      b.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  tag(0, 'RIFF');
  b.setUint32(4, 36 + dataBytes, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  b.setUint32(16, 16, Endian.little);
  b.setUint16(20, 1, Endian.little);
  b.setUint16(22, channels, Endian.little);
  b.setUint32(24, sampleRate, Endian.little);
  b.setUint32(28, sampleRate * channels * 2, Endian.little);
  b.setUint16(32, channels * 2, Endian.little);
  b.setUint16(34, 16, Endian.little);
  tag(36, 'data');
  b.setUint32(40, dataBytes, Endian.little);
  for (var i = 0; i < interleaved.length; i++) {
    final v = (interleaved[i].clamp(-1.0, 1.0) * 32767).round();
    b.setInt16(44 + 2 * i, v, Endian.little);
  }
  return b.buffer.asUint8List();
}
