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

  String get url => 'http://127.0.0.1:${_server.port}';

  Future<void> start() async {
    _server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    _server.listen(_handle);
  }

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
    } else if (req.method == 'PUT' && p.length == 5 && p[3] == 'chunks') {
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
        final r = ready;
        json(200, {
          'state': r == chunks ? 'done' : 'separating',
          'progress': chunks == 0 ? 0 : r / chunks,
          'vocals_ready': r,
          'timing': {'separate': 1.5},
        });
      }
    } else if (req.method == 'GET' && p.length == 5 && p[3] == 'vocals') {
      final i = int.parse(p[4]);
      res.statusCode = 200;
      res.headers.contentType = ContentType('audio', 'flac');
      res.add([...utf8.encode('v'), ...uploaded[i]!]);
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
        isA<SeparationException>().having(
          (e) => e.message,
          'message',
          contains('Boom'),
        ),
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
