import 'dart:async';
import 'dart:convert';
import 'dart:io';

/// Bố cục các đoạn bài gửi lên (biết sau khi máy giải mã xong bài).
class ChunkLayout {
  const ChunkLayout({
    required this.sampleRate,
    required this.channels,
    required this.frames,
    required this.chunkFrames,
    required this.chunks,
  });

  final int sampleRate;
  final int channels;
  final int frames;
  final int chunkFrames;
  final int chunks;
}

/// Nguồn các đoạn FLAC của bài (app: FlacExporter của engine; test: file giả).
abstract class ChunkSource {
  /// Chờ tới khi biết bố cục (máy giải mã xong).
  Future<ChunkLayout> layout();

  /// Chờ tới khi đoạn [index] ghi xong.
  Future<File> chunk(int index);

  void cancel();
  void dispose();
}

enum SeparationStage { preparing, uploading, separating, downloading, done }

class SeparationProgress {
  const SeparationProgress({
    required this.stage,
    required this.fraction,
    required this.chunks,
    required this.uploaded,
    required this.downloaded,
    required this.elapsed,
  });

  final SeparationStage stage;

  /// 0..1 cho thanh tiến độ.
  final double fraction;
  final int chunks;
  final int uploaded;
  final int downloaded;
  final Duration elapsed;
}

/// Kết quả: các đoạn giọng đã tải (đúng thứ tự) + số đo.
class SeparationResult {
  const SeparationResult({
    required this.vocals,
    required this.elapsed,
    required this.serverSeconds,
  });

  final List<File> vocals;

  /// Từ lúc bắt đầu tới khi tải xong đoạn giọng cuối.
  final Duration elapsed;

  /// Thời gian server dành cho tách (giây), null nếu server không báo.
  final double? serverSeconds;
}

class SeparationException implements Exception {
  SeparationException(this.message, {this.status});

  /// Câu báo lỗi tiếng Việt cho người dùng.
  final String message;
  final int? status;

  @override
  String toString() => 'SeparationException($status): $message';
}

/// Huỷ một lần tách đang chạy.
class SeparationCancel {
  bool _cancelled = false;
  bool get cancelled => _cancelled;
  void cancel() => _cancelled = true;
}

class ServerInfo {
  const ServerInfo({required this.separator, required this.version});

  final String separator;
  final String version;
}

/// Gửi bài lên server tách giọng theo đoạn và tải giọng về (docs/adr/006-vocal-separation.md):
/// tạo job → upload từng đoạn ngay khi máy xuất xong → hỏi trạng thái → tải giọng từng đoạn ngay
/// khi server xong → xoá job. Upload, tách, tải về chạy gối nhau.
class SeparationClient {
  SeparationClient({
    required this.baseUrl,
    required this.deviceId,
    HttpClient? http,
    this.pollInterval = const Duration(milliseconds: 500),
    this.retries = 3,
  }) : _http = http ?? HttpClient()
         ..connectionTimeout = const Duration(seconds: 5);

  final String baseUrl;
  final String deviceId;
  final Duration pollInterval;
  final int retries;
  final HttpClient _http;

  void close() => _http.close(force: true);

  Future<ServerInfo> health() async {
    final r = await _send('GET', '/v1/health', auth: false);
    final j = _json(r.body);
    if (r.status != 200 || j['ok'] != true) {
      throw SeparationException(
        'Máy chủ trả lời lạ (${r.status}).',
        status: r.status,
      );
    }
    return ServerInfo(
      separator: '${j['separator']}',
      version: '${j['version']}',
    );
  }

  Future<SeparationResult> separate(
    ChunkSource source,
    Directory outDir, {
    void Function(SeparationProgress p)? onProgress,
    SeparationCancel? cancel,
  }) async {
    final clock = Stopwatch()..start();
    var uploaded = 0;
    var downloaded = 0;
    var serverProgress = 0.0;
    var chunks = 0;
    String? jobId;
    Object? uploadError;

    void report(SeparationStage stage) {
      if (onProgress == null) return;
      final f = chunks == 0
          ? 0.0
          : 0.1 * uploaded / chunks +
                0.8 * serverProgress +
                0.1 * downloaded / chunks;
      onProgress(
        SeparationProgress(
          stage: stage,
          fraction: f.clamp(0.0, 1.0),
          chunks: chunks,
          uploaded: uploaded,
          downloaded: downloaded,
          elapsed: clock.elapsed,
        ),
      );
    }

    void checkCancel() {
      if (cancel?.cancelled ?? false) {
        throw SeparationException('Đã huỷ tách giọng.');
      }
    }

    try {
      report(SeparationStage.preparing);
      final layout = await source.layout();
      checkCancel();
      chunks = layout.chunks;
      final created = await _send(
        'POST',
        '/v1/jobs',
        body: utf8.encode(
          jsonEncode({
            'sample_rate': layout.sampleRate,
            'channels': layout.channels,
            'frames': layout.frames,
            'chunk_frames': layout.chunkFrames,
            'chunks': layout.chunks,
          }),
        ),
        contentType: 'application/json',
      );
      _expect(created, 200);
      jobId = '${_json(created.body)['id']}';

      // Upload song song với vòng hỏi trạng thái.
      final upload =
          () async {
            for (var i = 0; i < chunks; i++) {
              if (cancel?.cancelled ?? false) return;
              final file = await source.chunk(i);
              final r = await _send(
                'PUT',
                '/v1/jobs/$jobId/chunks/$i',
                body: await file.readAsBytes(),
                contentType: 'audio/flac',
              );
              _expect(r, 204);
              uploaded = i + 1;
              report(SeparationStage.uploading);
            }
          }().catchError((Object e) {
            uploadError = e;
          });

      final vocals = <File>[];
      double? serverSeconds;
      while (downloaded < chunks) {
        checkCancel();
        if (uploadError != null) throw uploadError!;
        final r = await _send('GET', '/v1/jobs/$jobId');
        _expect(r, 200);
        final st = _json(r.body);
        if (st['state'] == 'failed') {
          throw SeparationException(
            'Máy chủ tách lỗi: ${st['error'] ?? 'không rõ'}.',
          );
        }
        serverProgress = (st['progress'] as num?)?.toDouble() ?? 0.0;
        final ready = (st['vocals_ready'] as num?)?.toInt() ?? 0;
        final timing = st['timing'];
        if (timing is Map && timing['separate'] is num) {
          serverSeconds = (timing['separate'] as num).toDouble();
        }
        while (downloaded < ready) {
          checkCancel();
          final v = await _send('GET', '/v1/jobs/$jobId/vocals/$downloaded');
          _expect(v, 200);
          final f = File(
            '${outDir.path}/vocals_${downloaded.toString().padLeft(3, '0')}.flac',
          );
          final tmp = File('${f.path}.part');
          await tmp.writeAsBytes(v.body, flush: true);
          await tmp.rename(f.path);
          vocals.add(f);
          downloaded++;
          report(SeparationStage.downloading);
        }
        if (downloaded < chunks) {
          report(
            uploaded < chunks
                ? SeparationStage.uploading
                : SeparationStage.separating,
          );
          await Future<void>.delayed(pollInterval);
        }
      }
      await upload;
      final id = jobId;
      jobId = null;
      unawaited(_deleteQuietly(id));
      report(SeparationStage.done);
      return SeparationResult(
        vocals: vocals,
        elapsed: clock.elapsed,
        serverSeconds: serverSeconds,
      );
    } finally {
      final id = jobId;
      if (id != null) {
        source.cancel();
        unawaited(_deleteQuietly(id));
      }
    }
  }

  Future<void> _deleteQuietly(String id) async {
    try {
      await _send('DELETE', '/v1/jobs/$id', tries: 1);
    } catch (_) {
      // Server tự xoá sau 24 giờ.
    }
  }

  // ---- HTTP ----

  Future<({int status, List<int> body})> _send(
    String method,
    String path, {
    List<int>? body,
    String? contentType,
    bool auth = true,
    int? tries,
  }) async {
    final attempts = tries ?? retries;
    Object? last;
    for (var a = 0; a < attempts; a++) {
      try {
        final req = await _http
            .openUrl(method, Uri.parse('$baseUrl$path'))
            .timeout(const Duration(seconds: 10));
        if (auth) req.headers.set('X-ChuanTone-Device', deviceId);
        if (contentType != null) req.headers.set('Content-Type', contentType);
        if (body != null) {
          req.contentLength = body.length;
          req.add(body);
        }
        final res = await req.close().timeout(const Duration(seconds: 60));
        final bytes = await res
            .fold<List<int>>(<int>[], (acc, b) => acc..addAll(b))
            .timeout(const Duration(seconds: 60));
        // Lỗi phía server (5xx): thử lại; lỗi do yêu cầu (4xx): báo ngay.
        if (res.statusCode >= 500 && a + 1 < attempts) {
          last = SeparationException(
            'Máy chủ lỗi (${res.statusCode}).',
            status: res.statusCode,
          );
          await Future<void>.delayed(Duration(milliseconds: 300 * (a + 1)));
          continue;
        }
        return (status: res.statusCode, body: bytes);
      } on SocketException catch (e) {
        last = e;
      } on HttpException catch (e) {
        last = e;
      } on TimeoutException catch (e) {
        last = e;
      }
      await Future<void>.delayed(Duration(milliseconds: 300 * (a + 1)));
    }
    if (last is SeparationException) throw last;
    throw SeparationException(
      'Không kết nối được máy chủ tách giọng. Kiểm tra Wi-Fi và địa chỉ máy chủ.',
    );
  }

  static Map<String, dynamic> _json(List<int> body) {
    try {
      final j = jsonDecode(utf8.decode(body));
      return j is Map<String, dynamic> ? j : const {};
    } catch (_) {
      return const {};
    }
  }

  static void _expect(({int status, List<int> body}) r, int status) {
    if (r.status == status) return;
    final msg = _json(r.body)['error'];
    throw SeparationException(switch (r.status) {
      404 =>
        'Máy chủ không còn giữ bài này (có thể đã khởi động lại). Thử lại.',
      413 => msg is String ? 'Bài $msg.' : 'Bài dài quá.',
      429 => 'Hôm nay đã tách quá nhiều bài. Thử lại vào ngày mai.',
      _ => 'Máy chủ từ chối (${r.status}${msg is String ? ': $msg' : ''}).',
    }, status: r.status);
  }
}
