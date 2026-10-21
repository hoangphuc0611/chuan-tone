import 'package:chuantone/engine/labels.dart';
import 'package:chuantone/widgets/pitch_meter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

PitchState _state({
  bool voiced = true,
  double cents = 0,
  bool enabled = true,
}) => PitchState(
  voiced: voiced,
  f0Hz: 440,
  confidence: 0.95,
  inputNote: 69 + cents / 100,
  targetNote: voiced ? 69 : null,
  centsOff: cents,
  correctionCents: -cents,
  enabled: enabled,
);

Widget _wrap(Widget w) => MaterialApp(home: Scaffold(body: w));

void main() {
  test('nhãn tone và nốt', () {
    expect(keyLabel(const MusicKey(9, MusicScale.naturalMinor)), 'La thứ (Am)');
    expect(keyLabel(const MusicKey(0, MusicScale.major)), 'Đô trưởng (C)');
    expect(
      keyLabel(const MusicKey(7, MusicScale.pentatonicMajor)),
      'Son ngũ cung trưởng (G)',
    );
    expect(
      keyLabel(const MusicKey(1, MusicScale.harmonicMinor)),
      'Đô# thứ hoà âm (C#m)',
    );
    expect(midiNoteLabel(69), 'La4');
    expect(midiNoteLabel(60), 'Đô4');
    expect(correctionPresetLabel(CorrectionPreset.singer), 'Ca sĩ');
  });

  testWidgets('đồng hồ cao độ: chưa hát / đúng / lệch', (tester) async {
    await tester.pumpWidget(_wrap(const PitchMeter(state: null)));
    expect(find.text('Hãy hát để xem cao độ'), findsOneWidget);

    await tester.pumpWidget(_wrap(PitchMeter(state: _state(cents: 4))));
    expect(find.text('La4'), findsOneWidget);
    expect(find.text('Đúng tone'), findsOneWidget);

    await tester.pumpWidget(_wrap(PitchMeter(state: _state(cents: -32))));
    expect(find.text('Thấp 32 cent'), findsOneWidget);
    expect(find.text('Đang chỉnh +32 cent'), findsOneWidget);

    await tester.pumpWidget(
      _wrap(PitchMeter(state: _state(cents: 80, enabled: false))),
    );
    expect(find.text('Cao 80 cent'), findsOneWidget);
    expect(find.textContaining('Đang chỉnh'), findsNothing);
  });
}
