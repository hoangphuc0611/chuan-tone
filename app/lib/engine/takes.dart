import 'dart:io';

import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/latency_history.dart';
import '../data/song_ref.dart';
import '../data/take_store.dart';
import 'audio_service.dart';
import 'take_alignment.dart';

/// Thông số chuỗi giọng / trộn chụp lúc bấm Thu (Phòng thử) — đưa vào [TakeInfo] khi dừng.
class TakeSnapshot {
  const TakeSnapshot({
    required this.genre,
    required this.correction,
    required this.retuneMs,
    required this.key,
    required this.songTimeline,
    required this.tuningCents,
    required this.tempoBpm,
    required this.fxMix,
    required this.gateOn,
    required this.transpose,
    required this.voiceGain,
    required this.beatGain,
  });

  /// Từ trạng thái hiện tại của [AudioService] + engine.
  factory TakeSnapshot.of(
    AudioService s,
    ToneCoreEngine e, {
    required double voiceGain,
    required double beatGain,
  }) {
    final analysis = s.songAnalysis;
    return TakeSnapshot(
      genre: s.genre,
      correction: s.correction,
      retuneMs: s.correctionRetuneMs,
      key: s.currentKey,
      songTimeline: s.followSong && analysis != null && analysis.hasModulation
          ? analysis.timeline
          : const [],
      tuningCents: analysis?.tuningCents ?? 0,
      tempoBpm: e.tempo,
      fxMix: s.fxMix,
      gateOn: s.gateOn,
      transpose: e.playback.audibleTranspose,
      voiceGain: voiceGain,
      beatGain: beatGain,
    );
  }

  final Genre genre;
  final CorrectionPreset? correction;
  final double? retuneMs;
  final MusicKey key;

  /// Chuyển tone của bài theo thời gian beat gốc, tone gốc (rỗng = không theo bài).
  final List<KeySegment> songTimeline;
  final double tuningCents;
  final double tempoBpm;
  final double fxMix;
  final bool gateOn;
  final int transpose;
  final double voiceGain;
  final double beatGain;
}

/// Ghép số đo của engine ([RecordStatus]) + thông số lúc hát + độ trễ chọn theo đường ra.
TakeInfo takeInfoFor({
  required SongRef song,
  required RecordStatus status,
  required TakeSnapshot snap,
  required List<LatencyEntry> latencyHistory,
  DateTime? now,
}) {
  final rt = roundTripFor(
    route: status.route,
    reportedMs: status.inputLatencyMs + status.outputLatencyMs,
    history: latencyHistory,
  );
  final rate = status.sampleRate;
  return TakeInfo(
    songName: song.name,
    songSize: song.size,
    createdAt: now ?? DateTime.now(),
    sampleRate: rate,
    frames: status.frames,
    droppedFrames: status.droppedFrames,
    startBeatFrame: status.startBeatFrame,
    startHostTimeNs: status.startHostTimeNs,
    inputLatencyMs: status.inputLatencyMs,
    outputLatencyMs: status.outputLatencyMs,
    pitchLatencyFrames: status.pitchLatencyFrames,
    outputLookaheadFrames: status.outputLookaheadFrames,
    route: status.route,
    roundTripMs: rt.ms,
    latencySource: rt.source,
    genre: snap.genre,
    correction: snap.correction,
    retuneMs: snap.retuneMs,
    key: snap.key,
    timeline: rate == 0
        ? const []
        : takeTimeline(
            snap.songTimeline,
            startMs: status.startBeatFrame * 1000 / rate,
            transpose: snap.transpose,
          ),
    tuningCents: snap.tuningCents,
    tempoBpm: snap.tempoBpm,
    fxMix: snap.fxMix,
    gateOn: snap.gateOn,
    transpose: snap.transpose,
    voiceGain: snap.voiceGain,
    beatGain: snap.beatGain,
  );
}

/// Thông số render bản thu theo lựa chọn hiện tại (preset lấy theo thể loại).
RenderSettings renderSettingsFor(
  TakeInfo i, {
  required FxPreset preset,
  RenderFormat format = RenderFormat.m4a,
  bool normalize = true,
}) => RenderSettings(
  voiceOffsetFrames: i.voiceOffsetFrames,
  startFrame: i.trimStartFrame,
  endFrame: i.trimEndFrame,
  fx: preset,
  fxWet: i.fxMix,
  tempoBpm: i.tempoBpm,
  correction: i.correction,
  retuneMs: i.retuneMs,
  key: i.key,
  tuningCents: i.tuningCents,
  keyTimeline: i.timeline,
  gate: i.gateOn,
  voiceGain: i.voiceGain,
  beatGain: i.beatGain,
  normalize: normalize,
  format: format,
);

/// Thu âm trong Phòng thử: [start] mở thư mục + bắt đầu thu, [stop] dừng + lưu `take.json`.
class TakeSession {
  TakeSession({TakeStore? store, LatencyHistory? latency})
    : _store = store ?? TakeStore(),
      _latency = latency ?? LatencyHistory();

  final TakeStore _store;
  final LatencyHistory _latency;
  Directory? _dir;
  TakeSnapshot? _snap;

  bool get active => _dir != null;

  Future<void> start(ToneCoreEngine e, TakeSnapshot snap) async {
    final dir = await _store.newTakeDir();
    try {
      e.startRecording(dir.path);
    } catch (_) {
      await dir.delete(recursive: true);
      rethrow;
    }
    _dir = dir;
    _snap = snap;
  }

  /// Dừng và lưu bản thu. Lỗi ghi file → [ToneCoreException] (thư mục bị xoá).
  Future<Take> stop(ToneCoreEngine e, SongRef song) async {
    final dir = _dir!;
    final snap = _snap!;
    _dir = null;
    _snap = null;
    try {
      e.stopRecording();
    } catch (_) {
      await dir.delete(recursive: true);
      rethrow;
    }
    final info = takeInfoFor(
      song: song,
      status: e.recordStatus,
      snap: snap,
      latencyHistory: parseLatencyHistory(await _latency.load()),
    );
    await _store.save(dir, info);
    return Take(dir, info);
  }
}
