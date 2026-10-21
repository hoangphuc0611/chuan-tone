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

/// "La thứ (Am)", "Đô trưởng (C)", "Son ngũ cung trưởng (G)".
String keyLabel(MusicKey k) {
  final letter = noteLetter(k.root);
  final symbol = switch (k.scale) {
    MusicScale.naturalMinor ||
    MusicScale.harmonicMinor ||
    MusicScale.pentatonicMinor => '${letter}m',
    _ => letter,
  };
  if (k.scale == MusicScale.chromatic) return 'Tự do (12 nốt)';
  return '${noteNameVi(k.root)} ${scaleLabel(k.scale)} ($symbol)';
}

String correctionPresetLabel(CorrectionPreset p) => switch (p) {
  CorrectionPreset.natural => 'Tự nhiên',
  CorrectionPreset.singer => 'Ca sĩ',
  CorrectionPreset.robot => 'Robot',
};
