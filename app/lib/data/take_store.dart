import 'dart:convert';
import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/take_alignment.dart';
import '../engine/take_alignment.dart' as align show voiceOffsetFrames;
import 'app_dirs.dart';

/// Thông tin một bản thu (M7, docs/adr/007-recording-sync.md) — lưu ở `take.json` cạnh
/// `dry.wav` / `wet.wav` / `beat.wav`. Engine ghi thô; độ lệch giọng ↔ beat tính khi đọc.
class TakeInfo {
  const TakeInfo({
    required this.songName,
    required this.songSize,
    required this.createdAt,
    required this.sampleRate,
    required this.frames,
    this.droppedFrames = 0,
    this.startBeatFrame = 0,
    this.startHostTimeNs = 0,
    this.inputLatencyMs = 0,
    this.outputLatencyMs = 0,
    this.pitchLatencyFrames = 0,
    this.outputLookaheadFrames = 0,
    this.route = AudioRoute.unknown,
    this.roundTripMs = 0,
    this.latencySource = LatencySource.reported,
    this.nudgeMs = 0,
    this.genre = Genre.dry,
    this.correction,
    this.retuneMs,
    this.key = const MusicKey(9, MusicScale.naturalMinor),
    this.timeline = const [],
    this.tuningCents = 0,
    this.tempoBpm = 100,
    this.fxMix = 1,
    this.gateOn = false,
    this.transpose = 0,
    this.voiceGain = 1,
    this.beatGain = 1,
    this.trimStartFrame = 0,
    this.trimEndFrame = 0,
    this.videoFile,
    this.videoFirstFrameNs = 0,
    this.videoDroppedFrames = 0,
  });

  final String songName;
  final int songSize;
  final DateTime createdAt;
  final int sampleRate;
  final int frames;
  final int droppedFrames;
  final int startBeatFrame;
  final int startHostTimeNs;
  final double inputLatencyMs;
  final double outputLatencyMs;
  final int pitchLatencyFrames;
  final int outputLookaheadFrames;
  final AudioRoute route;

  /// Độ trễ khứ hồi dùng để bù (chọn theo đường ra lúc thu — [roundTripFor]).
  final double roundTripMs;
  final LatencySource latencySource;

  /// Người dùng chỉnh "Giọng sớm / muộn": dương = giọng ra sớm hơn.
  final double nudgeMs;

  // Chuỗi giọng lúc hát (Nghe lại / render dùng lại, đổi được).
  final Genre genre;
  final CorrectionPreset? correction;
  final double? retuneMs;

  /// Key đang nghe lúc bắt đầu thu (đã cộng tone đổi).
  final MusicKey key;

  /// Chuyển tone theo thời gian bản thu (đã cộng tone đổi); rỗng = dùng [key].
  final List<KeySegment> timeline;
  final double tuningCents;
  final double tempoBpm;
  final double fxMix;
  final bool gateOn;
  final int transpose;

  // Trộn / cắt ở màn Nghe lại.
  final double voiceGain;
  final double beatGain;
  final int trimStartFrame;

  /// 0 = hết bài.
  final int trimEndFrame;

  /// Video camera trước quay cùng lúc (tên file trong thư mục bản thu; null = không quay).
  final String? videoFile;

  /// Mốc khung hình đầu theo đồng hồ máy (ns, cùng gốc với [startHostTimeNs]).
  final int videoFirstFrameNs;
  final int videoDroppedFrames;

  Duration get duration => sampleRate == 0
      ? Duration.zero
      : Duration(microseconds: frames * 1000000 ~/ sampleRate);

  /// Giọng khô dịch sớm bao nhiêu frame để khớp beat.
  int get voiceOffsetFrames => align.voiceOffsetFrames(
    roundTripMs: roundTripMs + nudgeMs,
    sampleRate: sampleRate,
    lookaheadFrames: outputLookaheadFrames,
  );

  TakeInfo copyWith({
    double? nudgeMs,
    Genre? genre,
    CorrectionPreset? Function()? correction,
    double? Function()? retuneMs,
    double? fxMix,
    bool? gateOn,
    double? voiceGain,
    double? beatGain,
    int? trimStartFrame,
    int? trimEndFrame,
  }) => TakeInfo(
    songName: songName,
    songSize: songSize,
    createdAt: createdAt,
    sampleRate: sampleRate,
    frames: frames,
    droppedFrames: droppedFrames,
    startBeatFrame: startBeatFrame,
    startHostTimeNs: startHostTimeNs,
    inputLatencyMs: inputLatencyMs,
    outputLatencyMs: outputLatencyMs,
    pitchLatencyFrames: pitchLatencyFrames,
    outputLookaheadFrames: outputLookaheadFrames,
    route: route,
    roundTripMs: roundTripMs,
    latencySource: latencySource,
    nudgeMs: nudgeMs ?? this.nudgeMs,
    genre: genre ?? this.genre,
    correction: correction != null ? correction() : this.correction,
    retuneMs: retuneMs != null ? retuneMs() : this.retuneMs,
    key: key,
    timeline: timeline,
    tuningCents: tuningCents,
    tempoBpm: tempoBpm,
    fxMix: fxMix ?? this.fxMix,
    gateOn: gateOn ?? this.gateOn,
    transpose: transpose,
    voiceGain: voiceGain ?? this.voiceGain,
    beatGain: beatGain ?? this.beatGain,
    trimStartFrame: trimStartFrame ?? this.trimStartFrame,
    trimEndFrame: trimEndFrame ?? this.trimEndFrame,
    videoFile: videoFile,
    videoFirstFrameNs: videoFirstFrameNs,
    videoDroppedFrames: videoDroppedFrames,
  );

  Map<String, dynamic> toJson() => {
    'version': 1,
    'song': {'name': songName, 'size': songSize},
    'createdAt': createdAt.toIso8601String(),
    'sampleRate': sampleRate,
    'frames': frames,
    'droppedFrames': droppedFrames,
    'startBeatFrame': startBeatFrame,
    'startHostTimeNs': startHostTimeNs,
    'inputLatencyMs': inputLatencyMs,
    'outputLatencyMs': outputLatencyMs,
    'pitchLatencyFrames': pitchLatencyFrames,
    'outputLookaheadFrames': outputLookaheadFrames,
    'route': route.name,
    'roundTripMs': roundTripMs,
    'latencySource': latencySource.name,
    'nudgeMs': nudgeMs,
    'genre': genre.name,
    'correction': correction?.name,
    'retuneMs': retuneMs,
    'key': _keyJson(key),
    'timeline': [
      for (final s in timeline)
        {
          'startMs': s.start.inMicroseconds / 1000,
          'endMs': s.end.inMicroseconds / 1000,
          'key': _keyJson(s.key),
        },
    ],
    'tuningCents': tuningCents,
    'tempoBpm': tempoBpm,
    'fxMix': fxMix,
    'gateOn': gateOn,
    'transpose': transpose,
    'voiceGain': voiceGain,
    'beatGain': beatGain,
    'trimStartFrame': trimStartFrame,
    'trimEndFrame': trimEndFrame,
    'video': videoFile == null
        ? null
        : {
            'file': videoFile,
            'firstFrameNs': videoFirstFrameNs,
            'droppedFrames': videoDroppedFrames,
          },
  };

  /// Đọc lại; thiếu / hỏng trường bắt buộc → [FormatException].
  factory TakeInfo.fromJson(Map<String, dynamic> j) {
    T pick<T>(List<T> values, Object? name, T fallback) =>
        values.where((v) => (v as Enum).name == name).firstOrNull ?? fallback;
    double d(Object? v, double fallback) => v is num ? v.toDouble() : fallback;
    int i(Object? v, int fallback) => v is int ? v : fallback;
    final song = j['song'];
    final rate = j['sampleRate'];
    final frames = j['frames'];
    if (song is! Map || song['name'] is! String || song['size'] is! int) {
      throw const FormatException('take.json thiếu bài');
    }
    if (rate is! int || rate <= 0 || frames is! int || frames < 0) {
      throw const FormatException('take.json thiếu tần số / độ dài');
    }
    return TakeInfo(
      songName: song['name'] as String,
      songSize: song['size'] as int,
      createdAt:
          DateTime.tryParse(j['createdAt'] as String? ?? '') ??
          DateTime.fromMillisecondsSinceEpoch(0),
      sampleRate: rate,
      frames: frames,
      droppedFrames: i(j['droppedFrames'], 0),
      startBeatFrame: i(j['startBeatFrame'], 0),
      startHostTimeNs: i(j['startHostTimeNs'], 0),
      inputLatencyMs: d(j['inputLatencyMs'], 0),
      outputLatencyMs: d(j['outputLatencyMs'], 0),
      pitchLatencyFrames: i(j['pitchLatencyFrames'], 0),
      outputLookaheadFrames: i(j['outputLookaheadFrames'], 0),
      route: pick(AudioRoute.values, j['route'], AudioRoute.unknown),
      roundTripMs: d(j['roundTripMs'], 0),
      latencySource: pick(
        LatencySource.values,
        j['latencySource'],
        LatencySource.reported,
      ),
      nudgeMs: d(j['nudgeMs'], 0),
      genre: pick(Genre.values, j['genre'], Genre.dry),
      correction: j['correction'] == null
          ? null
          : pick<CorrectionPreset?>(
              CorrectionPreset.values,
              j['correction'],
              null,
            ),
      retuneMs: j['retuneMs'] is num ? (j['retuneMs'] as num).toDouble() : null,
      key: _keyFromJson(j['key']) ?? const MusicKey(9, MusicScale.naturalMinor),
      timeline: [
        if (j['timeline'] case final List list)
          for (final s in list)
            if (s is Map && _keyFromJson(s['key']) != null)
              KeySegment(
                start: Duration(
                  microseconds: (d(s['startMs'], 0) * 1000).round(),
                ),
                end: Duration(microseconds: (d(s['endMs'], 0) * 1000).round()),
                key: _keyFromJson(s['key'])!,
              ),
      ],
      tuningCents: d(j['tuningCents'], 0),
      tempoBpm: d(j['tempoBpm'], 100),
      fxMix: d(j['fxMix'], 1),
      gateOn: j['gateOn'] == true,
      transpose: i(j['transpose'], 0),
      voiceGain: d(j['voiceGain'], 1),
      beatGain: d(j['beatGain'], 1),
      trimStartFrame: i(j['trimStartFrame'], 0),
      trimEndFrame: i(j['trimEndFrame'], 0),
      videoFile: switch (j['video']) {
        {'file': final String f} => f,
        _ => null,
      },
      videoFirstFrameNs: switch (j['video']) {
        {'firstFrameNs': final int t} => t,
        _ => 0,
      },
      videoDroppedFrames: switch (j['video']) {
        {'droppedFrames': final int n} => n,
        _ => 0,
      },
    );
  }

  static Map<String, Object> _keyJson(MusicKey k) => {
    'root': k.root,
    'scale': k.scale.name,
  };

  static MusicKey? _keyFromJson(Object? v) {
    if (v is! Map || v['root'] is! int) return null;
    final root = v['root'] as int;
    final scale = MusicScale.values
        .where((s) => s.name == v['scale'])
        .firstOrNull;
    if (root < 0 || root > 11 || scale == null) return null;
    return MusicKey(root, scale);
  }
}

/// Chuyển tone của bài (thời gian của beat gốc, tone gốc) → theo thời gian bản thu bắt đầu ở
/// [startMs] của bài, cộng [transpose] nửa cung. Đoạn đã qua trước lúc thu bị bỏ.
List<KeySegment> takeTimeline(
  List<KeySegment> song, {
  required double startMs,
  required int transpose,
}) {
  final start = Duration(microseconds: (startMs * 1000).round());
  return [
    for (final s in song)
      if (s.end > start)
        KeySegment(
          start: s.start > start ? s.start - start : Duration.zero,
          end: s.end - start,
          key: MusicKey((s.key.root + transpose) % 12, s.key.scale),
          confidence: s.confidence,
        ),
  ];
}

/// Một bản thu trên đĩa.
class Take {
  const Take(this.dir, this.info);

  final Directory dir;
  final TakeInfo info;

  String get id => dir.uri.pathSegments.where((s) => s.isNotEmpty).last;
  String get dryPath => '${dir.path}/dry.wav';
  String get wetPath => '${dir.path}/wet.wav';
  String get beatPath => '${dir.path}/beat.wav';

  /// Video camera (null nếu không quay / file đã mất).
  String? get videoPath {
    final f = info.videoFile;
    if (f == null) return null;
    final path = '${dir.path}/$f';
    return File(path).existsSync() ? path : null;
  }

  /// Tổng dung lượng thư mục (byte).
  int sizeBytes() {
    var n = 0;
    for (final f in dir.listSync().whereType<File>()) {
      n += f.lengthSync();
    }
    return n;
  }
}

/// Các bản thu trong `<dữ liệu app>/takes/<thời điểm>/`.
class TakeStore {
  TakeStore({this.baseDir});

  final Directory? baseDir;

  Directory get root => Directory('${(baseDir ?? appDataDir()).path}/takes');

  /// Thư mục mới cho một lần thu (tên theo thời điểm, không trùng).
  Future<Directory> newTakeDir([DateTime? now]) async {
    final t = now ?? DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    final base =
        '${t.year}${two(t.month)}${two(t.day)}-${two(t.hour)}${two(t.minute)}${two(t.second)}';
    var name = base;
    for (var k = 2; await Directory('${root.path}/$name').exists(); k++) {
      name = '$base-$k';
    }
    return Directory('${root.path}/$name').create(recursive: true);
  }

  Future<void> save(Directory dir, TakeInfo info) =>
      writeFileAtomic(File('${dir.path}/take.json'), jsonEncode(info.toJson()));

  /// Bản thu đủ file + take.json đọc được, mới nhất trước.
  Future<List<Take>> list() async {
    if (!await root.exists()) return const [];
    final out = <Take>[];
    await for (final e in root.list()) {
      if (e is! Directory) continue;
      final t = await load(e);
      if (t != null) out.add(t);
    }
    out.sort((a, b) => b.info.createdAt.compareTo(a.info.createdAt));
    return out;
  }

  Future<Take?> load(Directory dir) async {
    try {
      final j = jsonDecode(await File('${dir.path}/take.json').readAsString());
      if (j is! Map<String, dynamic>) return null;
      final take = Take(dir, TakeInfo.fromJson(j));
      if (!await File(take.dryPath).exists() ||
          !await File(take.beatPath).exists()) {
        return null;
      }
      return take;
    } catch (_) {
      return null;
    }
  }

  Future<void> delete(Take take) async {
    if (await take.dir.exists()) await take.dir.delete(recursive: true);
  }
}
