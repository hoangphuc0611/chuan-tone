import 'package:tonecore_flutter/tonecore_flutter.dart';

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
