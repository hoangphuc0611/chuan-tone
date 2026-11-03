import 'dart:async';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/analysis_cache.dart';
import '../data/app_settings.dart';
import '../data/song_ref.dart';
import '../data/stem_store.dart';
import 'background.dart';
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
/// đoạn giọng vào [store]. Chạy nền ngắn khi app vào nền (M8 B7). [resume]: tiếp tục lần tách dở
/// (máy chủ của lần đó). Bị ngắt ([SeparationException.resumable]: mất mạng, tạm dừng) → giữ job +
/// phần đã tải để tiếp tục; lỗi khác → xoá phần tải dở.
Future<SeparationResult> separateSong(
  SongRef song, {
  required String server,
  required AppSettings settings,
  required StemStore store,
  SeparationCancel? cancel,
  void Function(SeparationProgress p)? onProgress,
  PendingSeparation? resume,
}) async {
  final tmp = await Directory.systemTemp.createTemp('tach');
  final source = NativeChunkSource(song.path, tmp);
  final url = resume?.server ?? server;
  final client = SeparationClient(
    baseUrl: url,
    deviceId: await settings.deviceId(),
  );
  try {
    final out = resume != null
        ? await store
              .dirFor(name: song.name, size: song.size)
              .create(recursive: true)
        : await store.prepare(name: song.name, size: song.size);
    final r = await inBackground(
      'Tách giọng',
      () => client.separate(
        source,
        out,
        cancel: cancel,
        onProgress: onProgress,
        resume: resume?.job,
        onJob: (job) => store.savePending(
          PendingSeparation(
            name: song.name,
            size: song.size,
            server: url,
            job: job,
            startedAt: DateTime.now(),
          ),
        ),
      ),
    );
    await store.markDone(
      name: song.name,
      size: song.size,
      chunks: r.vocals.length,
      elapsed: r.elapsed,
      serverSeconds: r.serverSeconds,
    );
    return r;
  } on SeparationException catch (e) {
    final keep =
        e.resumable &&
        await store.pendingFor(name: song.name, size: song.size) != null;
    if (!keep) await store.delete(name: song.name, size: song.size);
    rethrow;
  } catch (_) {
    await store.delete(name: song.name, size: song.size);
    rethrow;
  } finally {
    source.dispose();
    client.close();
    unawaited(tmp.delete(recursive: true).then((_) {}, onError: (_) {}));
  }
}

/// Bỏ lần tách dở: xoá job trên máy chủ (lỗi thì thôi, máy chủ tự xoá sau 24 giờ) và phần đã tải.
Future<void> discardSeparationJob(
  PendingSeparation p, {
  required AppSettings settings,
  required StemStore store,
}) async {
  final client = SeparationClient(
    baseUrl: p.server,
    deviceId: await settings.deviceId(),
    retries: 1,
  );
  try {
    await client.deleteJob(p.job.id);
  } finally {
    client.close();
  }
  await store.delete(name: p.name, size: p.size);
}

/// Lưu địa chỉ máy chủ tách giọng sau khi kiểm tra kết nối; rỗng = xoá. Trả câu báo kết quả và
/// [changed] = đã lưu (địa chỉ mới ở [url], null nếu đã xoá).
Future<({String message, bool changed, String? url})> saveSeparationServer(
  AppSettings settings,
  String input,
) async {
  if (input.trim().isEmpty) {
    await settings.setSeparationServer(null);
    return (message: 'Đã xoá địa chỉ.', changed: true, url: null);
  }
  final url = normalizeServerUrl(input);
  if (url == null) {
    return (message: 'Địa chỉ không hợp lệ.', changed: false, url: null);
  }
  final c = SeparationClient(
    baseUrl: url,
    deviceId: await settings.deviceId(),
    retries: 1,
  );
  try {
    final info = await c.health();
    await settings.setSeparationServer(url);
    return (
      message: 'Đã kết nối (bộ tách ${info.separator}).',
      changed: true,
      url: url,
    );
  } on SeparationException catch (err) {
    return (message: err.message, changed: false, url: null);
  } finally {
    c.close();
  }
}
