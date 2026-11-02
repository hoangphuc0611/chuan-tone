import 'dart:convert';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'library_db.dart';

/// Lưu kết quả phân tích bài (tone, chuyển tone, tuning, BPM, độ to) để mở lại bài không phải
/// phân tích lại. Bảng `analysis` của [LibraryDb] (M8; trước đó là `analysis_cache.json`).
///
/// Khoá = tên file + kích thước (như mọi kho khác của bài) + phiên bản engine (thuật toán đổi thì
/// phân tích lại).
class AnalysisCache {
  AnalysisCache({required this.engineVersion, this.baseDir});

  static const maxEntries = 200;

  final String engineVersion;

  /// Thư mục CSDL; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  Future<SongAnalysis?> lookup({
    required String name,
    required int size,
  }) async {
    final rows = await (await LibraryDb.open(baseDir)).query(
      'analysis',
      columns: ['json'],
      where: 'song_key = ? AND engine_version = ?',
      whereArgs: [songKey(name, size), engineVersion],
    );
    if (rows.isEmpty) return null;
    try {
      return analysisFromJson(
        jsonDecode(rows.first['json']! as String) as Map<String, dynamic>,
      );
    } catch (_) {
      return null;
    }
  }

  Future<void> store({
    required String name,
    required int size,
    required SongAnalysis analysis,
  }) async {
    final db = await LibraryDb.open(baseDir);
    await db.transaction((txn) async {
      await txn.rawInsert(
        'INSERT OR REPLACE INTO analysis(song_key, engine_version, json, stored_at) '
        'VALUES(?, ?, ?, ?)',
        [
          songKey(name, size),
          engineVersion,
          jsonEncode(analysisToJson(analysis)),
          LibraryDb.now(),
        ],
      );
      await txn.execute(
        'DELETE FROM analysis WHERE rowid NOT IN '
        '(SELECT rowid FROM analysis ORDER BY stored_at DESC LIMIT ?)',
        [maxEntries],
      );
    });
  }

  /// Xoá mọi kết quả của bài (mọi phiên bản engine) — khi xoá bài khỏi thư viện.
  Future<void> remove({required String name, required int size}) async {
    await (await LibraryDb.open(baseDir)).delete(
      'analysis',
      where: 'song_key = ?',
      whereArgs: [songKey(name, size)],
    );
  }
}

Map<String, dynamic> _keyToJson(MusicKey k) => {
  'root': k.root,
  'scale': k.scale.name,
};

MusicKey _keyFromJson(Map<String, dynamic> j) =>
    MusicKey(j['root'] as int, MusicScale.values.byName(j['scale'] as String));

Map<String, dynamic> analysisToJson(SongAnalysis a) => {
  'durationMs': a.duration.inMilliseconds,
  'sampleRate': a.sampleRate,
  'channels': a.channels,
  'key': a.key == null ? null : _keyToJson(a.key!),
  'keyConfidence': a.keyConfidence,
  'tuningCents': a.tuningCents,
  'bpm': a.bpm,
  'bpmConfidence': a.bpmConfidence,
  'lufs': a.integratedLufs,
  'peakDb': a.samplePeakDb,
  'timeline': [
    for (final s in a.timeline)
      {
        'startMs': s.start.inMilliseconds,
        'endMs': s.end.inMilliseconds,
        'key': _keyToJson(s.key),
        'confidence': s.confidence,
      },
  ],
};

SongAnalysis analysisFromJson(Map<String, dynamic> j) {
  double? opt(String k) => (j[k] as num?)?.toDouble();
  return SongAnalysis(
    duration: Duration(milliseconds: j['durationMs'] as int),
    sampleRate: j['sampleRate'] as int,
    channels: j['channels'] as int,
    key: j['key'] == null
        ? null
        : _keyFromJson(j['key'] as Map<String, dynamic>),
    keyConfidence: (j['keyConfidence'] as num).toDouble(),
    tuningCents: opt('tuningCents'),
    bpm: opt('bpm'),
    bpmConfidence: (j['bpmConfidence'] as num).toDouble(),
    integratedLufs: opt('lufs'),
    samplePeakDb: (j['peakDb'] as num).toDouble(),
    timeline: [
      for (final s in (j['timeline'] as List).cast<Map<String, dynamic>>())
        KeySegment(
          start: Duration(milliseconds: s['startMs'] as int),
          end: Duration(milliseconds: s['endMs'] as int),
          key: _keyFromJson(s['key'] as Map<String, dynamic>),
          confidence: (s['confidence'] as num).toDouble(),
        ),
    ],
  );
}
