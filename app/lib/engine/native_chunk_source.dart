import 'dart:async';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'separation_client.dart';

/// Nguồn đoạn của app: engine giải mã bài bằng codec của máy, nén FLAC 44,1 kHz (tần số model) theo
/// đoạn 20 s ở luồng nền ([FlacExporter]); đoạn nào xong là gửi được ngay.
class NativeChunkSource implements ChunkSource {
  NativeChunkSource(String songPath, Directory tmpDir)
    : _prefix = '${tmpDir.path}/up',
      _x = FlacExporter.create() {
    _x.start(
      songPath,
      _prefix,
      sampleRate: sampleRate,
      chunkSeconds: chunkSeconds,
      maxSeconds: maxSeconds,
    );
  }

  static const sampleRate = 44100;
  static const chunkSeconds = 20.0;
  static const maxSeconds = 600.0; // giới hạn của server (spec mục 4)

  final String _prefix;
  final FlacExporter _x;
  bool _disposed = false;

  ExportStatus _status() {
    final s = _x.status;
    if (s.state == AnalysisState.failed || s.state == AnalysisState.cancelled) {
      throw SeparationException(switch (s.result) {
        TcResult.notSupported =>
          'Bài dài quá 10 phút hoặc định dạng không đọc được.',
        TcResult.cancelled => 'Đã huỷ tách giọng.',
        _ => 'Không đọc được bài (${ToneCoreException.resultName(s.result)}).',
      });
    }
    return s;
  }

  @override
  Future<ChunkLayout> layout() async {
    while (true) {
      final s = _status();
      if (s.chunksTotal > 0) {
        return ChunkLayout(
          sampleRate: s.sampleRate,
          channels: s.channels,
          frames: s.frames,
          chunkFrames: s.chunkFrames,
          chunks: s.chunksTotal,
        );
      }
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
  }

  @override
  Future<File> chunk(int index) async {
    while (true) {
      final s = _status();
      if (s.chunksDone > index) {
        return File(FlacExporter.chunkPath(_prefix, index));
      }
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
  }

  @override
  void cancel() {
    if (!_disposed) _x.cancel();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _x.dispose();
  }
}
