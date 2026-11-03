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

/// Định dạng + vị trí phần dữ liệu của một file WAV PCM 16-bit / float 32-bit.
class WavFormat {
  const WavFormat({
    required this.sampleRate,
    required this.channels,
    required this.isFloat,
    required this.dataOffset,
    required this.dataBytes,
  });

  final int sampleRate;
  final int channels;
  final bool isFloat;

  /// Byte đầu tiên của phần dữ liệu.
  final int dataOffset;

  /// Số byte dữ liệu (đã cắt theo độ dài thật của file).
  final int dataBytes;

  int get bytesPerSample => isFloat ? 4 : 2;
  int get frames => dataBytes ~/ (bytesPerSample * channels);
}

/// Tìm `fmt ` + `data` trong [bytes] (cả file, hoặc phần đầu file với [fileLength] = độ dài thật).
/// Không đúng định dạng → [FormatException].
WavFormat parseWavFormat(Uint8List bytes, {int? fileLength}) {
  final d = ByteData.sublistView(bytes);
  final length = fileLength ?? bytes.length;
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
    if (id == 'fmt ' && bytes.length - body >= 16) {
      format = d.getUint16(body, Endian.little);
      channels = d.getUint16(body + 2, Endian.little);
      rate = d.getUint32(body + 4, Endian.little);
      bits = d.getUint16(body + 14, Endian.little);
      if (format == 0xFFFE && size >= 26 && bytes.length - body >= 26) {
        format = d.getUint16(body + 24, Endian.little);
      }
    } else if (id == 'data') {
      if (format == null || channels == 0) break;
      final isFloat = format == 3 && bits == 32;
      final isPcm16 = format == 1 && bits == 16;
      if (!isFloat && !isPcm16) {
        throw FormatException('WAV $format/$bits bit chưa hỗ trợ');
      }
      return WavFormat(
        sampleRate: rate,
        channels: channels,
        isFloat: isFloat,
        dataOffset: body,
        dataBytes: (length - body).clamp(0, size),
      );
    }
    pos = body + size + (size & 1);
  }
  throw const FormatException('WAV thiếu fmt / data');
}

/// Đọc WAV PCM 16-bit hoặc float 32-bit (file bản thu của engine, docs/adr/007-recording-sync.md).
/// Không đúng định dạng → [FormatException].
WavAudio readWavFile(String path) => parseWav(File(path).readAsBytesSync());

WavAudio parseWav(Uint8List bytes) {
  final w = parseWavFormat(bytes);
  final d = ByteData.sublistView(bytes);
  final count = w.frames * w.channels;
  final out = Float32List(count);
  for (var i = 0; i < count; i++) {
    final at = w.dataOffset + i * w.bytesPerSample;
    out[i] = w.isFloat
        ? d.getFloat32(at, Endian.little)
        : d.getInt16(at, Endian.little) / 32768.0;
  }
  return WavAudio(sampleRate: w.sampleRate, channels: w.channels, samples: out);
}

/// File WAV mở để đọc từng đoạn (không nạp cả bài vào bộ nhớ).
class WavReader {
  WavReader._(this._file, this.format);

  /// Mở [path] và đọc phần đầu file. Không đúng định dạng → [FormatException].
  static WavReader open(String path) {
    final f = File(path).openSync();
    try {
      final length = f.lengthSync();
      final head = f.readSync(length < 65536 ? length : 65536);
      return WavReader._(f, parseWavFormat(head, fileLength: length));
    } catch (_) {
      f.closeSync();
      rethrow;
    }
  }

  final RandomAccessFile _file;
  final WavFormat format;

  /// [count] frame từ frame [start], trộn các kênh về mono; phần nằm ngoài file = 0.
  Float32List readMono(int start, int count) {
    final w = format;
    final out = Float32List(count);
    final from = start < 0 ? 0 : start;
    final to = start + count > w.frames ? w.frames : start + count;
    if (to <= from) return out;
    final frameBytes = w.bytesPerSample * w.channels;
    _file.setPositionSync(w.dataOffset + from * frameBytes);
    final bytes = _file.readSync((to - from) * frameBytes);
    final d = ByteData.sublistView(bytes);
    final n = bytes.length ~/ frameBytes;
    for (var i = 0; i < n; i++) {
      var sum = 0.0;
      for (var c = 0; c < w.channels; c++) {
        final at = (i * w.channels + c) * w.bytesPerSample;
        sum += w.isFloat
            ? d.getFloat32(at, Endian.little)
            : d.getInt16(at, Endian.little) / 32768.0;
      }
      out[from - start + i] = sum / w.channels;
    }
    return out;
  }

  void close() => _file.closeSync();
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
