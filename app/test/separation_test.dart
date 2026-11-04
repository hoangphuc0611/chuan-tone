import 'dart:convert';
import 'dart:io';

import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/stem_store.dart';
import 'package:chuantone/engine/separation_client.dart';
import 'package:flutter_test/flutter_test.dart';

/// Server giả theo giao thức ADR-006: "tách" ngay đoạn nào đã nhận (giọng = "v" + nội dung đoạn).
class FakeServer {
  late HttpServer _server;
  final uploaded = <int, List<int>>{};
  final deleted = <String>[];
  final devices = <String?>[];
  String state = 'uploading';
  int failCreateWith = 0; // ≠ 0: POST /v1/jobs trả mã này
  int flakyStatusLeft = 0; // số lần GET trạng thái trả 500 trước khi trả đúng
  bool failSeparation = false;
  int chunks = 0;
  final puts = <int>[]; // thứ tự các đoạn nhận được (kể cả gửi lại)
  bool lost = false; // "khởi động lại": không còn job nào
  bool holdVocals = false; // chưa trả đoạn giọng nào
  bool echoVocals =
      false; // giọng = đúng nội dung đoạn (FLAC thật → engine nạp được)

  String get url => 'http://127.0.0.1:${_server.port}';

  /// [port] = 0: cổng bất kỳ; cổng cũ: "mở lại máy chủ" cùng địa chỉ.
  Future<void> start({int port = 0}) async {
    _server = await HttpServer.bind(InternetAddress.loopbackIPv4, port);
    _server.listen(_handle);
  }

  int get port => _server.port;

  Future<void> stop() => _server.close(force: true);

  int get ready {
    var n = 0;
    while (uploaded.containsKey(n)) {
      n++;
    }
    return n;
  }

  Future<void> _handle(HttpRequest req) async {
    final body = await req.fold<List<int>>([], (a, b) => a..addAll(b));
    final p = req.uri.pathSegments;
    final res = req.response;
    devices.add(req.headers.value('X-ChuanTone-Device'));
    void json(int status, Object o) {
      res.statusCode = status;
      res.headers.contentType = ContentType.json;
      res.write(jsonEncode(o));
    }

    if (req.method == 'GET' && req.uri.path == '/v1/health') {
      json(200, {'ok': true, 'separator': 'fake', 'version': '0.1.0'});
    } else if (req.method == 'POST' && req.uri.path == '/v1/jobs') {
      if (failCreateWith != 0) {
        json(failCreateWith, {'error': 'dài quá 10 phút'});
      } else {
        chunks = (jsonDecode(utf8.decode(body)) as Map)['chunks'] as int;
        json(200, {'id': 'job1', 'state': 'uploading'});
      }
    } else if (lost && p.length >= 3 && p[1] == 'jobs') {
      json(404, {'error': 'không có job này (có thể đã hết hạn)'});
    } else if (req.method == 'PUT' && p.length == 5 && p[3] == 'chunks') {
      puts.add(int.parse(p[4]));
      uploaded[int.parse(p[4])] = body;
      res.statusCode = 204;
    } else if (req.method == 'GET' && p.length == 3 && p[1] == 'jobs') {
      if (flakyStatusLeft > 0) {
        flakyStatusLeft--;
        res.statusCode = 500;
      } else if (failSeparation) {
        json(200, {
          'state': 'failed',
          'error': 'tách lỗi: Boom',
          'vocals_ready': 0,
        });
      } else {
        final r = holdVocals ? 0 : ready;
        json(200, {
          'state': r == chunks ? 'done' : 'separating',
          'progress': chunks == 0 ? 0 : r / chunks,
          'chunks': chunks,
          'chunks_received': uploaded.length,
          'vocals_ready': r,
          'timing': {'separate': 1.5},
        });
      }
    } else if (req.method == 'GET' && p.length == 5 && p[3] == 'vocals') {
      final i = int.parse(p[4]);
      res.statusCode = 200;
      res.headers.contentType = ContentType('audio', 'flac');
      res.add(
        echoVocals ? uploaded[i]! : [...utf8.encode('v'), ...uploaded[i]!],
      );
    } else if (req.method == 'DELETE') {
      deleted.add(p[2]);
      res.statusCode = 204;
    } else {
      json(404, {'error': 'không có'});
    }
    await res.close();
  }
}

class FakeSource implements ChunkSource {
  FakeSource(this.dir, this.n, {this.delay = Duration.zero});

  final Directory dir;
  final int n;
  final Duration delay;
  bool cancelled = false;
  bool disposed = false;

  @override
  Future<ChunkLayout> layout() async => ChunkLayout(
    sampleRate: 44100,
    channels: 2,
    frames: n * 1000 - 10,
    chunkFrames: 1000,
    chunks: n,
  );

  @override
  Future<File> chunk(int index) async {
    await Future<void>.delayed(delay);
    final f = File('${dir.path}/c$index.flac');
    await f.writeAsString('doan-$index');
    return f;
  }

  @override
  void cancel() => cancelled = true;

  @override
  void dispose() => disposed = true;
}

/// Nguồn không được dùng tới (máy chủ đã có đủ đoạn).
class _NoSource implements ChunkSource {
  bool used = false;

  @override
  Future<ChunkLayout> layout() async {
    used = true;
    throw StateError('không được đọc lại bài');
  }

  @override
  Future<File> chunk(int index) async {
    used = true;
    throw StateError('không được đọc lại bài');
  }

  @override
  void cancel() {}

  @override
  void dispose() {}
}

/// HttpClient giả: mọi yêu cầu hỏng như khi máy chủ ngắt kết nối trên Linux.
class _ResetClient implements HttpClient {
  @override
  set connectionTimeout(Duration? _) {}

  @override
  Future<HttpClientRequest> openUrl(String method, Uri url) async =>
      throw const OSError('Connection reset by peer', 104);

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  late Directory dir;
  late FakeServer server;
  setUp(() async {
    dir = Directory.systemTemp.createTempSync('separation');
    server = FakeServer();
    await server.start();
  });
  tearDown(() async {
    await server.stop();
    dir.deleteSync(recursive: true);
  });

  SeparationClient client() => SeparationClient(
    baseUrl: server.url,
    deviceId: 'may-1',
    pollInterval: const Duration(milliseconds: 5),
  );

  test('health', () async {
    final info = await client().health();
    expect(info.separator, 'fake');
  });

  test('máy chủ ngắt giữa chừng kiểu Linux (OSError thô, không phải SocketException): '
      'báo mất kết nối, tiếp tục được', () async {
    final c = SeparationClient(
      baseUrl: server.url,
      deviceId: 'may-1',
      http: _ResetClient(),
      retries: 2,
    );
    await expectLater(
      c.health(),
      throwsA(
        isA<SeparationException>()
            .having((e) => e.resumable, 'resumable', isTrue)
            .having((e) => e.details, 'details', contains('reset by peer')),
      ),
    );
  });

  test('gửi đủ đoạn, tải giọng đúng thứ tự, xoá job', () async {
    final out = Directory('${dir.path}/out')..createSync();
    final progress = <SeparationProgress>[];
    final r = await client().separate(
      FakeSource(dir, 4, delay: const Duration(milliseconds: 3)),
      out,
      onProgress: progress.add,
    );
    expect(r.vocals.map((f) => f.path.split('/').last), [
      'vocals_000.flac',
      'vocals_001.flac',
      'vocals_002.flac',
      'vocals_003.flac',
    ]);
    expect(r.vocals[2].readAsStringSync(), 'vdoan-2');
    expect(r.serverSeconds, 1.5);
    expect(progress.first.stage, SeparationStage.preparing);
    expect(progress.last.stage, SeparationStage.done);
    expect(progress.last.fraction, 1.0);
    for (var i = 1; i < progress.length; i++) {
      expect(
        progress[i].fraction,
        greaterThanOrEqualTo(progress[i - 1].fraction),
      );
    }
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, ['job1']);
    expect(server.devices.where((d) => d != null).toSet(), {'may-1'});
    expect(out.listSync().where((f) => f.path.endsWith('.part')), isEmpty);
  });

  test('lỗi 5xx tạm thời được thử lại', () async {
    server.flakyStatusLeft = 2;
    final r = await client().separate(FakeSource(dir, 2), dir);
    expect(r.vocals, hasLength(2));
  });

  test('server tách lỗi → báo lỗi, huỷ nguồn, xoá job', () async {
    server.failSeparation = true;
    final src = FakeSource(dir, 3);
    await expectLater(
      client().separate(src, dir),
      throwsA(
        isA<SeparationException>()
            .having((e) => e.message, 'message', contains('Hãy thử lại'))
            .having((e) => e.details, 'details', contains('Boom')),
      ),
    );
    expect(src.cancelled, isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, ['job1']);
  });

  test('bài quá dài / hết lượt → câu báo tiếng Việt', () async {
    server.failCreateWith = 413;
    await expectLater(
      client().separate(FakeSource(dir, 1), dir),
      throwsA(
        isA<SeparationException>().having(
          (e) => e.message,
          'm',
          'Bài dài quá 10 phút.',
        ),
      ),
    );
    server.failCreateWith = 429;
    await expectLater(
      client().separate(FakeSource(dir, 1), dir),
      throwsA(
        isA<SeparationException>().having((e) => e.status, 'status', 429),
      ),
    );
  });

  test('huỷ giữa chừng', () async {
    final cancel = SeparationCancel();
    final src = FakeSource(dir, 50, delay: const Duration(milliseconds: 20));
    final f = client().separate(
      src,
      dir,
      cancel: cancel,
      onProgress: (p) {
        if (p.uploaded >= 2) cancel.cancel();
      },
    );
    await expectLater(f, throwsA(isA<SeparationException>()));
    expect(src.cancelled, isTrue);
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, ['job1']);
  });

  test('tạm dừng giữa chừng: giữ job; tiếp tục chỉ gửi đoạn còn thiếu, tải tiếp giọng', () async {
    final out = Directory('${dir.path}/out')..createSync();
    final cancel = SeparationCancel();
    SeparationJob? job;
    final src = FakeSource(dir, 6, delay: const Duration(milliseconds: 15));
    await expectLater(
      client().separate(
        src,
        out,
        cancel: cancel,
        onJob: (j) async => job = j,
        onProgress: (p) {
          if (p.uploaded >= 3) cancel.pause();
        },
      ),
      throwsA(
        isA<SeparationException>().having(
          (e) => e.resumable,
          'resumable',
          isTrue,
        ),
      ),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, isEmpty); // job còn trên máy chủ
    expect(job!.id, 'job1');
    expect(job!.chunks, 6);
    final sentBefore = server.puts.length;
    final r = await client().separate(FakeSource(dir, 6), out, resume: job);
    expect(r.vocals, hasLength(6));
    expect(r.vocals[5].readAsStringSync(), 'vdoan-5');
    final resent = server.puts.sublist(sentBefore);
    expect(resent.first, greaterThanOrEqualTo(3)); // không gửi lại từ đầu
    await Future<void>.delayed(const Duration(milliseconds: 50));
    expect(server.deleted, ['job1']);
  });

  test('đã gửi đủ đoạn: tiếp tục không cần đọc lại bài', () async {
    final out = Directory('${dir.path}/out')..createSync();
    await client().separate(FakeSource(dir, 3), out); // máy chủ đã có đủ 3 đoạn
    server.deleted.clear();
    for (final f in out.listSync()) {
      f.deleteSync(); // như chưa tải đoạn giọng nào
    }
    final src = _NoSource();
    final r = await client().separate(
      src,
      out,
      resume: const SeparationJob(id: 'job1', chunks: 3),
    );
    expect(r.vocals, hasLength(3));
    expect(src.used, isFalse);
  });

  test(
    'máy chủ đã mất job (khởi động lại) → báo tách lại, không tiếp tục',
    () async {
      server.lost = true;
      await expectLater(
        client().separate(
          FakeSource(dir, 2),
          dir,
          resume: const SeparationJob(id: 'job1', chunks: 2),
        ),
        throwsA(
          isA<SeparationException>()
              .having((e) => e.status, 'status', 404)
              .having((e) => e.resumable, 'resumable', isFalse)
              .having((e) => e.message, 'message', contains('tách lại')),
        ),
      );
    },
  );

  test('mất kết nối giữa chừng → lỗi tiếp tục được, giữ job; có mạng lại thì tách tiếp', () async {
    final out = Directory('${dir.path}/out')..createSync();
    final port = server.port;
    SeparationJob? job;
    var stopped = false;
    final c = SeparationClient(
      baseUrl: server.url,
      deviceId: 'may-1',
      pollInterval: const Duration(milliseconds: 5),
      retries: 1,
    );
    await expectLater(
      c.separate(
        FakeSource(dir, 6, delay: const Duration(milliseconds: 15)),
        out,
        onJob: (j) async => job = j,
        onProgress: (p) {
          if (p.uploaded >= 2 && !stopped) {
            stopped = true;
            server.stop();
          }
        },
      ),
      throwsA(
        isA<SeparationException>()
            .having((e) => e.resumable, 'resumable', isTrue)
            .having((e) => e.message, 'message', contains('Wi-Fi')),
      ),
    );
    expect(server.deleted, isEmpty);
    await server.start(port: port);
    final r = await client().separate(FakeSource(dir, 6), out, resume: job);
    expect(r.vocals, hasLength(6));
  });

  test('không có server → báo kiểm tra Wi-Fi', () async {
    final url = server.url;
    await server.stop();
    final c = SeparationClient(baseUrl: url, deviceId: 'x', retries: 1);
    await expectLater(
      c.health(),
      throwsA(
        isA<SeparationException>().having(
          (e) => e.message,
          'm',
          contains('Wi-Fi'),
        ),
      ),
    );
    await server.start(); // cho tearDown
  });

  group('cài đặt & nơi lưu stem', () {
    test('địa chỉ server được chuẩn hoá', () {
      expect(normalizeServerUrl('192.168.1.5:8000'), 'http://192.168.1.5:8000');
      expect(
        normalizeServerUrl(' http://may.local:8000/ '),
        'http://may.local:8000',
      );
      expect(
        normalizeServerUrl('https://tach.chuantone.vn'),
        'https://tach.chuantone.vn',
      );
      expect(normalizeServerUrl(''), isNull);
      expect(normalizeServerUrl('ftp://x'), isNull);
    });

    test('lưu cài đặt, mã máy cố định', () async {
      final s = AppSettings(baseDir: dir);
      expect(await s.separationServer(), isNull);
      expect(await s.separationConsent(), isFalse);
      await s.setSeparationServer('http://1.2.3.4:8000');
      await s.setSeparationConsent(true);
      final id = await s.deviceId();
      expect(id, hasLength(32));
      final again = AppSettings(baseDir: dir);
      expect(await again.separationServer(), 'http://1.2.3.4:8000');
      expect(await again.separationConsent(), isTrue);
      expect(await again.deviceId(), id);
    });

    test(
      'lần tách dở: lưu, đọc lại, quá 24 giờ thì bỏ, tách xong thì xoá',
      () async {
        final store = StemStore(baseDir: dir);
        final t0 = DateTime(2026, 11, 3, 10);
        PendingSeparation pending(String name) => PendingSeparation(
          name: name,
          size: 1,
          server: 'http://may-chu:8000',
          job: const SeparationJob(id: 'j1', chunks: 3),
          startedAt: t0,
        );
        await store.prepare(name: 'a.m4a', size: 1);
        await store.savePending(pending('a.m4a'));
        final p = (await store.pendingFor(
          name: 'a.m4a',
          size: 1,
          now: t0.add(const Duration(hours: 1)),
        ))!;
        expect(p.job.id, 'j1');
        expect(p.job.chunks, 3);
        expect(p.server, 'http://may-chu:8000');
        expect(p.startedAt, t0);
        expect(await store.vocalsFor(name: 'a.m4a', size: 1), isNull);
        await store.savePending(pending('b.m4a'));
        expect(
          (await store.pending(now: t0.add(const Duration(hours: 1))))
              .map((p) => p.name),
          unorderedEquals(['a.m4a', 'b.m4a']),
        );
        // Tách xong → hết dở.
        await store.markDone(name: 'a.m4a', size: 1, chunks: 3);
        expect(await store.pendingFor(name: 'a.m4a', size: 1, now: t0), isNull);
        // Quá 24 giờ: máy chủ đã xoá job → bỏ cả phần đã tải.
        expect(
          await store.pendingFor(
            name: 'b.m4a',
            size: 1,
            now: t0.add(const Duration(hours: 25)),
          ),
          isNull,
        );
        expect(store.dirFor(name: 'b.m4a', size: 1).existsSync(), isFalse);
        // File hỏng → coi như không có.
        File('${store.dirFor(name: 'c.m4a', size: 1).path}/pending.json')
          ..createSync(recursive: true)
          ..writeAsStringSync('{');
        expect(await store.pendingFor(name: 'c.m4a', size: 1), isNull);
      },
    );

    test('stem chỉ dùng được khi đủ đoạn và có meta', () async {
      final store = StemStore(baseDir: dir);
      expect(await store.vocalsFor(name: 'a.m4a', size: 1), isNull);
      final d = await store.prepare(name: 'a.m4a', size: 1);
      for (var i = 0; i < 3; i++) {
        File('${d.path}/${StemStore.chunkName(i)}').writeAsStringSync('$i');
      }
      expect(
        await store.vocalsFor(name: 'a.m4a', size: 1),
        isNull,
      ); // chưa có meta
      await store.markDone(name: 'a.m4a', size: 1, chunks: 3);
      final v = await store.vocalsFor(name: 'a.m4a', size: 1);
      expect(v, hasLength(3));
      expect(
        await store.resultFor(name: 'a.m4a', size: 1),
        isNull,
      ); // không có số đo
      await store.markDone(
        name: 'a.m4a',
        size: 1,
        chunks: 3,
        elapsed: const Duration(milliseconds: 64200),
        serverSeconds: 56.4,
      );
      final last = (await store.resultFor(name: 'a.m4a', size: 1))!;
      expect(last.elapsed, const Duration(milliseconds: 64200));
      expect(last.serverSeconds, 56.4);
      expect(last.vocals.map((f) => f.path), v);
      expect(v!.last, endsWith('vocals_002.flac'));
      expect(await store.vocalsFor(name: 'a.m4a', size: 2), isNull);
      File(v[1]).deleteSync();
      expect(
        await store.vocalsFor(name: 'a.m4a', size: 1),
        isNull,
      ); // thiếu đoạn
      expect(
        store.dirFor(name: 'a.m4a', size: 1).path,
        isNot(store.dirFor(name: 'b.m4a', size: 1).path),
      );
      await store.delete(name: 'a.m4a', size: 1);
      expect(store.dirFor(name: 'a.m4a', size: 1).existsSync(), isFalse);
    });
  });
}
