import 'dart:io';

import 'package:chuantone/data/latency_history.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory dir;
  setUp(() => dir = Directory.systemTemp.createTempSync('latency_hist'));
  tearDown(() => dir.deleteSync(recursive: true));

  test('lưu và đọc lại, mới nhất lên đầu', () async {
    final store = LatencyHistory(baseDir: dir);
    expect(await store.load(), isEmpty);
    var h = await store.add('a', []);
    h = await store.add('b', h);
    expect(h, ['b', 'a']);
    expect(await LatencyHistory(baseDir: dir).load(), ['b', 'a']);
  });

  test('giữ tối đa 50 dòng', () async {
    final store = LatencyHistory(baseDir: dir);
    var h = <String>[];
    for (var i = 0; i < 60; i++) {
      h = await store.add('$i', h);
    }
    expect(h.length, LatencyHistory.maxEntries);
    expect(h.first, '59');
  });

  test('file hỏng → lịch sử rỗng', () async {
    File('${dir.path}/latency_history.json').writeAsStringSync('{hong');
    expect(await LatencyHistory(baseDir: dir).load(), isEmpty);
  });
}
