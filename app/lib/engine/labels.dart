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
