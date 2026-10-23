import 'dart:convert';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'app_dirs.dart';

/// Lưu kết quả phân tích bài (tone, chuyển tone, tuning, BPM, độ to) để mở lại bài không phải
/// phân tích lại.
///
/// Khoá = tên file + kích thước (file_picker trên iOS chép file sang thư mục tạm mới mỗi lần chọn
/// nên đường dẫn không dùng làm khoá được; fingerprint nội dung làm ở M6/M8) + phiên bản engine
/// (thuật toán đổi thì phân tích lại).
class AnalysisCache {
  AnalysisCache({required this.engineVersion, this.baseDir});

  static const maxEntries = 200;

  final String engineVersion;

  /// Thư mục lưu file; để null thì dùng thư mục dữ liệu của app (test truyền thư mục tạm).
  final Directory? baseDir;

  File get _file =>
      File('${(baseDir ?? appDataDir()).path}/analysis_cache.json');

  String _keyOf(String name, int size) => '$engineVersion|$name|$size';

  Future<Map<String, dynamic>> _readAll() async {
    try {
      final f = _file;
      if (!await f.exists()) return {};
      final data = jsonDecode(await f.readAsString());
      if (data is Map<String, dynamic>) return data;
    } catch (_) {
      // File hỏng → coi như chưa có gì.
    }
    return {};
  }

  Future<SongAnalysis?> lookup({
    required String name,
    required int size,
  }) async {
    final entry = (await _readAll())[_keyOf(name, size)];
    if (entry is! Map<String, dynamic>) return null;
    try {
      return analysisFromJson(entry);
    } catch (_) {
      return null;
    }
  }

  Future<void> store({
    required String name,
    required int size,
    required SongAnalysis analysis,
  }) async {
    final all = await _readAll();
    final key = _keyOf(name, size);
    all.remove(key); // đưa xuống cuối = mới nhất
    all[key] = analysisToJson(analysis);
    while (all.length > maxEntries) {
      all.remove(all.keys.first);
    }
    await writeFileAtomic(_file, jsonEncode(all));
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
