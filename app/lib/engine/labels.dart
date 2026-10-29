import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'genre_rules.dart';
import 'separation_client.dart';

/// Nhãn tiếng Việt cho UI.
String routeLabel(AudioRoute r) => switch (r) {
  AudioRoute.unknown => 'Chưa rõ',
  AudioRoute.speaker => 'Loa ngoài',
  AudioRoute.wired => 'Tai nghe có dây',
  AudioRoute.bluetooth => 'Bluetooth',
  AudioRoute.usb => 'USB / soundcard',
  AudioRoute.receiver => 'Loa thoại',
};

String apiLabel(AudioApi a) => switch (a) {
  AudioApi.none => '—',
  AudioApi.aaudio => 'AAudio',
  AudioApi.opensles => 'OpenSL ES',
  AudioApi.remoteio => 'RemoteIO',
};

String formatDuration(Duration d) {
  final m = d.inMinutes;
  final s = d.inSeconds % 60;
  return '$m:${s.toString().padLeft(2, '0')}';
}

// ---- Nốt nhạc & tone ----

const _solfege = [
  'Đô',
  'Đô#',
  'Rê',
  'Rê#',
  'Mi',
  'Fa',
  'Fa#',
  'Son',
  'Son#',
  'La',
  'La#',
  'Si', //
];
const _letters = [
  'C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B', //
];

/// Tên nốt theo tên Việt (Đô Rê Mi…) cho cao độ lớp `pitchClass` (0 = Đô).
String noteNameVi(int pitchClass) => _solfege[pitchClass % 12];

/// Tên nốt theo ký hiệu quốc tế (C D E…).
String noteLetter(int pitchClass) => _letters[pitchClass % 12];

/// Nốt MIDI → "La4" (quãng 8 theo quy ước La4 = 440 Hz).
String midiNoteLabel(int midi) => '${noteNameVi(midi % 12)}${midi ~/ 12 - 1}';

String scaleLabel(MusicScale s) => switch (s) {
  MusicScale.major => 'trưởng',
  MusicScale.naturalMinor => 'thứ',
  MusicScale.harmonicMinor => 'thứ hoà âm',
  MusicScale.pentatonicMajor => 'ngũ cung trưởng',
  MusicScale.pentatonicMinor => 'ngũ cung thứ',
  MusicScale.chromatic => '12 nốt',
};

/// Ký hiệu hợp âm của tone: "Am", "C", "F#m".
String keySymbol(MusicKey k) {
  final letter = noteLetter(k.root);
  return switch (k.scale) {
    MusicScale.naturalMinor ||
    MusicScale.harmonicMinor ||
    MusicScale.pentatonicMinor => '${letter}m',
    _ => letter,
  };
}

/// "La thứ (Am)", "Đô trưởng (C)", "Son ngũ cung trưởng (G)".
String keyLabel(MusicKey k) {
  if (k.scale == MusicScale.chromatic) return 'Tự do (12 nốt)';
  return '${noteNameVi(k.root)} ${scaleLabel(k.scale)} (${keySymbol(k)})';
}

// ---- Đổi tone beat ----

/// "Gốc", "+2", "−3" (dấu trừ thật, không phải gạch nối).
String transposeLabel(int semitones) => semitones == 0
    ? 'Gốc'
    : semitones > 0
    ? '+$semitones'
    : '\u2212${-semitones}';

/// Tone sau khi đổi beat `semitones` nửa cung.
MusicKey transposedKey(MusicKey k, int semitones) =>
    MusicKey((k.root + semitones) % 12, k.scale);

/// Người dùng chọn tay tone đang NGHE khi beat đã đổi `semitones` → tone của beat gốc (engine
/// nhận tone gốc rồi tự cộng số nửa cung).
MusicKey baseKeyForHeard(MusicKey heard, int semitones) =>
    transposedKey(heard, -semitones);

// ---- Phân tích bài ----

String analysisStageLabel(AnalysisStage s) => switch (s) {
  AnalysisStage.none || AnalysisStage.decoding => 'Đang đọc bài…',
  AnalysisStage.loudness => 'Đang đo âm lượng…',
  AnalysisStage.resampling => 'Đang chuẩn bị…',
  AnalysisStage.separating => 'Đang tách tiếng trống…',
  AnalysisStage.spectrum => 'Đang nghe hoà âm…',
  AnalysisStage.key => 'Đang dò tone…',
  AnalysisStage.tempo => 'Đang dò nhịp…',
  AnalysisStage.done => 'Xong',
};

/// "La thứ (Am) – 92 BPM", như màn Phân tích bài trong spec.
String analysisSummary(SongAnalysis a) {
  final key = a.key == null ? 'Chưa dò được tone' : keyLabel(a.key!);
  final bpm = a.bpm;
  return bpm == null ? key : '$key – ${bpm.round()} BPM';
}

/// Chỉ nhắc khi beat lệch rõ (≥ 3 cent) so với chuẩn La 440 Hz.
String? tuningNote(SongAnalysis a) {
  final c = a.tuningCents;
  if (c == null || c.abs() < 3) return null;
  final sign = c > 0 ? '+' : '−';
  return 'Beat lệch $sign${c.abs().round()} cent so với chuẩn — chỉnh giọng đã theo đúng beat.';
}

/// "0:00 Am → 1:35 A → 2:40 Am"; null nếu bài không chuyển tone.
String? modulationSummary(SongAnalysis a) {
  if (!a.hasModulation) return null;
  return [
    for (final s in a.timeline)
      '${formatDuration(s.start)} ${keySymbol(s.key)}',
  ].join(' → ');
}

String correctionPresetLabel(CorrectionPreset p) => switch (p) {
  CorrectionPreset.natural => 'Tự nhiên',
  CorrectionPreset.singer => 'Ca sĩ',
  CorrectionPreset.robot => 'Robot',
};

// ---- Hiệu ứng giọng ----

String genreLabel(Genre g) => switch (g) {
  Genre.dry => 'Khô',
  Genre.bolero => 'Bolero',
  Genre.ballad => 'Ballad',
  Genre.pop => 'Nhạc trẻ',
  Genre.remix => 'Remix',
  Genre.rap => 'Rap',
  Genre.lofi => 'Lofi',
};

String _dec(double v) => v.toStringAsFixed(1).replaceAll('.', ',');

String _reverbTypeLabel(ReverbType t) => switch (t) {
  ReverbType.room => 'phòng',
  ReverbType.hall => 'hội trường',
  ReverbType.plate => 'plate',
};

String _delayLabel(FxPreset p) => switch (p.delayDivision) {
  DelayDivision.off => '',
  DelayDivision.quarter => '1/4 nốt',
  DelayDivision.eighth => '1/8 nốt',
  DelayDivision.dottedEighth => '1/8 chấm',
  DelayDivision.sixteenth => '1/16 nốt',
  DelayDivision.ms => '${p.delayMs.round()} ms',
};

/// "Vang hội trường 2,2 s · vọng 1/4 nốt · nén 3:1"; "Không hiệu ứng" cho preset Khô.
String fxSummary(FxPreset p) {
  final parts = [
    if (p.reverbEnabled)
      'Vang ${_reverbTypeLabel(p.reverbType)} ${_dec(p.reverbDecayS)} s',
    if (p.delayEnabled) 'vọng ${_delayLabel(p)}',
    if (p.compEnabled) 'nén ${p.compRatio.round()}:1',
  ];
  if (parts.isEmpty) return 'Không hiệu ứng';
  final s = parts.join(' · ');
  return s[0].toUpperCase() + s.substring(1);
}

/// "Tự nhiên 45 ms", "Tắt"; null khi preset giữ nguyên chỉnh giọng (Khô).
String? genreCorrectionLabel(GenreCorrection c) {
  if (c.keep) return null;
  final level = c.level;
  if (level == null) return 'Tắt';
  return '${correctionPresetLabel(level)} ${c.retuneMs!.round()} ms';
}

/// Vì sao chip này đang được chọn.
String genreSourceLabel(GenreSource s, {double? bpm}) => switch (s) {
  GenreSource.saved => 'Đã chọn cho bài này',
  GenreSource.name => 'Gợi ý theo tên bài',
  GenreSource.bpm =>
    bpm == null ? 'Gợi ý theo nhịp bài' : 'Gợi ý theo nhịp ${bpm.round()} BPM',
};

// ---- Tách giọng ca sĩ ----

String _secs(Duration d) => '${(d.inMilliseconds / 1000).round()} s';

/// "Đang gửi bài 3/12… (5 s)", "Đang tách giọng… 45% (20 s)"…
String separationStageLabel(SeparationProgress p) {
  final what = switch (p.stage) {
    SeparationStage.preparing => 'Đang chuẩn bị bài…',
    SeparationStage.uploading => 'Đang gửi bài ${p.uploaded}/${p.chunks}…',
    SeparationStage.separating =>
      'Đang tách giọng… ${(p.fraction * 100).round()}%',
    SeparationStage.downloading =>
      'Đang tải giọng về ${p.downloaded}/${p.chunks}…',
    SeparationStage.done => 'Xong',
  };
  return '$what (${_secs(p.elapsed)})';
}

/// "Tách xong sau 43 s (máy chủ tách 38 s)".
String separationDoneLabel(SeparationResult r) => [
  'Tách xong sau ${_secs(r.elapsed)}',
  if (r.serverSeconds != null) '(máy chủ tách ${r.serverSeconds!.round()} s)',
].join(' ');
