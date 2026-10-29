import 'dart:async';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/analysis_cache.dart';
import '../data/app_settings.dart';
import '../data/song_ref.dart';
import '../data/stem_store.dart';
import 'native_chunk_source.dart';
import 'separation_client.dart';

/// Phân tích bài (tone, chuyển tone, tuning, BPM, độ to) — lấy từ bộ nhớ nếu đã phân tích. Có
/// [vocals] (stem giọng đã tách): phân tích phần nhạc = bài − giọng, lưu riêng. Trả kết quả và thời
/// gian phân tích (null nếu lấy từ bộ nhớ). [onAnalyzer] nhận analyzer đang chạy (để dispose khi
/// đổi bài → [StateError]).
Future<(SongAnalysis, Duration?)> analyzeSong(
  SongRef song, {
  List<String>? vocals,
  required AnalysisCache cache,
  void Function(AnalysisStatus s)? onProgress,
  void Function(SongAnalyzer a)? onAnalyzer,
}) async {
  final cacheName = vocals == null ? song.name : '${song.name}#phan-nhac';
  final cached = await cache.lookup(name: cacheName, size: song.size);
  if (cached != null) return (cached, null);
  final analyzer = SongAnalyzer.create();
  onAnalyzer?.call(analyzer);
  final clock = Stopwatch()..start();
  try {
    final r = await analyzer.analyze(
      song.path,
      vocals: vocals ?? const [],
      onProgress: onProgress,
    );
    await cache.store(name: cacheName, size: song.size, analysis: r);
    return (r, clock.elapsed);
  } finally {
    analyzer.dispose();
  }
}

/// Tách giọng ca sĩ của [song] qua máy chủ [server] (docs/adr/006-vocal-separation.md), lưu các
/// đoạn giọng vào [store]. Lỗi → [SeparationException] (đã xoá phần tải dở).
Future<SeparationResult> separateSong(
  SongRef song, {
  required String server,
  required AppSettings settings,
  required StemStore store,
  SeparationCancel? cancel,
  void Function(SeparationProgress p)? onProgress,
}) async {
  final tmp = await Directory.systemTemp.createTemp('tach');
  final source = NativeChunkSource(song.path, tmp);
  final client = SeparationClient(
    baseUrl: server,
    deviceId: await settings.deviceId(),
  );
  try {
    final out = await store.prepare(name: song.name, size: song.size);
    final r = await client.separate(
      source,
      out,
      cancel: cancel,
      onProgress: onProgress,
    );
    await store.markDone(
      name: song.name,
      size: song.size,
      chunks: r.vocals.length,
    );
    return r;
  } catch (_) {
    await store.delete(name: song.name, size: song.size);
    rethrow;
  } finally {
    source.dispose();
    client.close();
    unawaited(tmp.delete(recursive: true).then((_) {}, onError: (_) {}));
  }
}
