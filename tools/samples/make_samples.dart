// Tạo bài mẫu "luyện giọng" cho app (M8 B3, docs/m8-plan.md điểm 2): nhạc đệm piano tổng hợp + bass
// + trống nhẹ + giai điệu dẫn (người hát nghe nốt cần hát), kèm lời .lrc có mốc từng chữ. Toàn bộ do
// code này tạo ra — không dùng bản thu, mẫu âm thanh hay bài hát của ai — nên app được phép đóng gói.
//
// Chạy (macOS, cần `afconvert` có sẵn của hệ điều hành để nén AAC):
//   fvm dart run tools/samples/make_samples.dart            # → app/assets/samples/*.m4a + *.lrc
//   fvm dart run tools/samples/make_samples.dart --wav      # giữ thêm WAV ở testdata/samples/ để kiểm
//                                                           #   bằng keylab (không commit)
//
// Tổng hợp cộng (additive): mỗi nốt piano là tổng các hoạ âm hơi lệch (inharmonic), mỗi hoạ âm tắt dần
// theo hàm mũ (hoạ âm cao tắt nhanh hơn). Sóng sin sinh bằng bộ dao động đệ quy
// y[n] = 2·cos(ω)·y[n−1] − y[n−2] và đường bao nhân hằng số mỗi mẫu — rẻ hơn gọi sin / exp từng mẫu.

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

const sr = 44100;

double hz(num midi) => 440.0 * math.pow(2, (midi - 69) / 12);

/// Bản trộn stereo.
class Mix {
  Mix(double seconds)
    : l = Float64List((seconds * sr).ceil()),
      r = Float64List((seconds * sr).ceil());

  final Float64List l;
  final Float64List r;

  int get length => l.length;

  /// Cộng tín hiệu mono [v] bắt đầu ở mẫu [at], pan −1 (trái) … 1 (phải), công suất đều.
  void add(int at, Float64List v, {double pan = 0}) {
    final a = (pan + 1) * math.pi / 4;
    final gl = math.cos(a) * math.sqrt2;
    final gr = math.sin(a) * math.sqrt2;
    for (var i = 0; i < v.length && at + i < length; i++) {
      if (at + i < 0) continue;
      l[at + i] += v[i] * gl;
      r[at + i] += v[i] * gr;
    }
  }
}

/// Nốt piano tổng hợp: [dur] giây giữ phím, nhả trong ~0,1 s.
Float64List piano(int midi, double dur, double vel) {
  final f0 = hz(midi);
  final n = ((dur + 0.6) * sr).round();
  final out = Float64List(n);
  final hold = (dur * sr).round();
  final rng = math.Random(midi);
  for (var h = 1; h <= 10; h++) {
    final fh = f0 * h * math.sqrt(1 + 0.0004 * h * h);
    if (fh > 0.45 * sr) break;
    final amp = vel * math.pow(h, -1.4) * (h == 2 ? 0.8 : 1.0);
    final tau = 1.8 / (1 + 0.55 * (h - 1)) * (f0 < 180 ? 1.4 : 1.0);
    final w = 2 * math.pi * fh / sr;
    final c2 = 2 * math.cos(w);
    final ph = rng.nextDouble() * 2 * math.pi;
    var y1 = math.sin(ph - w);
    var y2 = math.sin(ph - 2 * w);
    final decay = math.exp(-1 / (tau * sr));
    final releaseDecay = math.exp(-1 / (0.09 * sr));
    var env = 1.0;
    for (var i = 0; i < n; i++) {
      final y = c2 * y1 - y2;
      y2 = y1;
      y1 = y;
      env *= i < hold ? decay : releaseDecay;
      final attack = i < 180 ? i / 180 : 1.0; // ~4 ms
      out[i] += amp * env * attack * y;
    }
  }
  return out;
}

/// Giai điệu dẫn kiểu sáo: hoạ âm 1–3, vào 60 ms, rung nhẹ (5 Hz, ±6 cent) sau 0,4 s, nhả 150 ms.
Float64List guide(int midi, double dur, double vel) {
  final f0 = hz(midi);
  final n = ((dur + 0.15) * sr).round();
  final out = Float64List(n);
  var phase = 0.0;
  for (var i = 0; i < n; i++) {
    final t = i / sr;
    final vib = t > 0.4 ? math.min(1.0, (t - 0.4) / 0.3) : 0.0;
    final f = f0 * (1 + 0.0035 * vib * math.sin(2 * math.pi * 5 * t));
    phase += 2 * math.pi * f / sr;
    final env =
        math.min(1.0, t / 0.06) *
        (t > dur ? math.max(0.0, 1 - (t - dur) / 0.15) : 1.0);
    out[i] =
        vel *
        env *
        (math.sin(phase) +
            0.12 * math.sin(2 * phase) +
            0.05 * math.sin(3 * phase));
  }
  return out;
}

Float64List kick(double vel) {
  final n = (0.4 * sr).round();
  final out = Float64List(n);
  var phase = 0.0;
  for (var i = 0; i < n; i++) {
    final t = i / sr;
    phase += 2 * math.pi * (45 + 75 * math.exp(-t / 0.035)) / sr;
    out[i] = vel * math.exp(-t / 0.16) * math.sin(phase);
  }
  return out;
}

final _noise = math.Random(7);

/// Tiếng gõ nhẹ phách 2, 4: nhiễu lọc cao + thân 190 Hz.
Float64List snare(double vel) {
  final n = (0.25 * sr).round();
  final out = Float64List(n);
  var prev = 0.0;
  for (var i = 0; i < n; i++) {
    final t = i / sr;
    final x = _noise.nextDouble() * 2 - 1;
    final hp = x - prev;
    prev = x;
    out[i] =
        vel *
        (0.6 * hp * math.exp(-t / 0.07) +
            0.5 * math.sin(2 * math.pi * 190 * t) * math.exp(-t / 0.05));
  }
  return out;
}

Float64List hat(double vel) {
  final n = (0.08 * sr).round();
  final out = Float64List(n);
  var p1 = 0.0;
  var p2 = 0.0;
  for (var i = 0; i < n; i++) {
    final x = _noise.nextDouble() * 2 - 1;
    final d1 = x - p1;
    p1 = x;
    final d2 = d1 - p2;
    p2 = d1;
    out[i] = vel * d2 * math.exp(-(i / sr) / 0.02);
  }
  return out;
}

/// Hợp âm: chủ âm (0–11) + trưởng / thứ.
class Chord {
  const Chord(this.root, {this.minor = false});

  final int root;
  final bool minor;

  List<int> get pcs => [root, (root + (minor ? 3 : 4)) % 12, (root + 7) % 12];
}

const C = Chord(0);
const Dm = Chord(2, minor: true);
const E = Chord(4);
const F = Chord(5);
const G = Chord(7);
const Am = Chord(9, minor: true);

/// Một nốt dẫn: cao độ, phách bắt đầu (tính từ đầu ô nhịp), số phách giữ, chữ trong lời.
class Note {
  const Note(this.midi, this.beat, this.beats, this.word);

  final int midi;
  final double beat;
  final double beats;
  final String word;
}

/// Một ô nhịp 4/4: các hợp âm (đổi theo phách) + các nốt dẫn.
class Bar {
  const Bar(this.chords, [this.notes = const []]);

  /// (phách bắt đầu, hợp âm).
  final List<(double, Chord)> chords;
  final List<Note> notes;
}

/// Một dòng lời: ô nhịp bắt đầu, chữ cố định (dòng không có nốt) hoặc lấy từ các nốt.
class LyricLine {
  const LyricLine(this.bar, this.bars, [this.text]);

  final int bar;
  final int bars;
  final String? text;
}

class Sample {
  const Sample({
    required this.id,
    required this.title,
    required this.bpm,
    required this.bars,
    required this.lines,
    required this.voicing,
  });

  final String id;
  final String title;
  final double bpm;
  final List<Bar> bars;
  final List<LyricLine> lines;

  /// Khoảng cao độ (MIDI) xếp các nốt của hợp âm.
  final (int, int) voicing;

  double get beatSec => 60 / bpm;
  double get barSec => 4 * beatSec;
}

/// Nốt của hợp âm trong khoảng [lo, hi].
List<int> voice(Chord c, (int, int) range) => [
  for (final pc in c.pcs)
    for (var m = range.$1; m <= range.$2; m++)
      if (m % 12 == pc) m,
];

Mix render(Sample s) {
  final mix = Mix(s.bars.length * s.barSec + 1.5);
  for (var b = 0; b < s.bars.length; b++) {
    final bar = s.bars[b];
    final t0 = b * s.barSec;
    // Hợp âm: phách 1 mạnh, phách 3 nhẹ hơn (nếu không đổi hợp âm ở đó); bass theo hợp âm.
    for (var k = 0; k < bar.chords.length; k++) {
      final (beat, chord) = bar.chords[k];
      final until = k + 1 < bar.chords.length ? bar.chords[k + 1].$1 : 4.0;
      final hits = <double>[beat, if (beat < 2 && until > 2) 2];
      for (final h in hits) {
        final end = h == beat && hits.length > 1 ? 2.0 : until;
        final dur = (end - h) * s.beatSec;
        final at = ((t0 + h * s.beatSec) * sr).round();
        final vel = h == beat ? 0.16 : 0.11;
        for (final m in voice(chord, s.voicing)) {
          mix.add(at, piano(m, dur, vel), pan: (m % 7 - 3) / 12);
        }
        var bass = 36 + chord.root;
        if (bass > 45) bass -= 12;
        mix.add(at, piano(bass, dur, 0.24));
      }
    }
    // Trống: trống cái phách 1, 3; gõ phách 2, 4; hi-hat mỗi nửa phách.
    for (var beat = 0; beat < 4; beat++) {
      final at = ((t0 + beat * s.beatSec) * sr).round();
      if (beat.isEven) {
        mix.add(at, kick(0.5));
      } else {
        mix.add(at, snare(0.22), pan: -0.1);
      }
      for (final half in [0.0, 0.5]) {
        final ht = ((t0 + (beat + half) * s.beatSec) * sr).round();
        mix.add(ht, hat(half == 0 ? 0.07 : 0.045), pan: 0.35);
      }
    }
    for (final n in bar.notes) {
      final at = ((t0 + n.beat * s.beatSec) * sr).round();
      mix.add(at, guide(n.midi, n.beats * s.beatSec, 0.2));
    }
  }
  // Chuẩn hoá đỉnh về −1 dBFS.
  var peak = 0.0;
  for (var i = 0; i < mix.length; i++) {
    peak = math.max(peak, math.max(mix.l[i].abs(), mix.r[i].abs()));
  }
  final g = math.pow(10, -1 / 20) / peak;
  for (var i = 0; i < mix.length; i++) {
    mix.l[i] *= g;
    mix.r[i] *= g;
  }
  return mix;
}

Uint8List wav16(Mix m) {
  final n = m.length;
  final b = ByteData(44 + n * 4);
  void tag(int at, String s) {
    for (var i = 0; i < 4; i++) {
      b.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  tag(0, 'RIFF');
  b.setUint32(4, 36 + n * 4, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  b.setUint32(16, 16, Endian.little);
  b.setUint16(20, 1, Endian.little);
  b.setUint16(22, 2, Endian.little);
  b.setUint32(24, sr, Endian.little);
  b.setUint32(28, sr * 4, Endian.little);
  b.setUint16(32, 4, Endian.little);
  b.setUint16(34, 16, Endian.little);
  tag(36, 'data');
  b.setUint32(40, n * 4, Endian.little);
  for (var i = 0; i < n; i++) {
    b.setInt16(
      44 + 4 * i,
      (m.l[i].clamp(-1.0, 1.0) * 32767).round(),
      Endian.little,
    );
    b.setInt16(
      46 + 4 * i,
      (m.r[i].clamp(-1.0, 1.0) * 32767).round(),
      Endian.little,
    );
  }
  return b.buffer.asUint8List();
}

String stamp(double sec) {
  final cs = (sec * 100).round();
  final m = cs ~/ 6000;
  final s = (cs % 6000) / 100;
  return '${m.toString().padLeft(2, '0')}:${s.toStringAsFixed(2).padLeft(5, '0')}';
}

String lrc(Sample s) {
  final out = StringBuffer()
    ..writeln('[ti:${s.title}]')
    ..writeln('[ar:ChuẩnTone]')
    ..writeln('[by:tools/samples/make_samples.dart]');
  for (final line in s.lines) {
    final t0 = line.bar * s.barSec;
    if (line.text != null) {
      out.writeln('[${stamp(t0)}]${line.text}');
      continue;
    }
    final words = <String>[];
    double? end;
    for (var b = line.bar; b < line.bar + line.bars; b++) {
      for (final n in s.bars[b].notes) {
        final t = b * s.barSec + n.beat * s.beatSec;
        words.add('<${stamp(t)}>${n.word}');
        end = t + n.beats * s.beatSec;
      }
    }
    final first = words.first.substring(1, 9);
    out.writeln('[$first]${words.join(' ')}<${stamp(end!)}>');
  }
  return out.toString();
}

// ---- hai bài ----

const _names = {
  0: 'Đô',
  2: 'Rê',
  4: 'Mi',
  5: 'Fa',
  7: 'Son',
  9: 'La',
  11: 'Si',
};

Note held(int midi, {double beats = 3, double beat = 0, String? word}) =>
    Note(midi, beat, beats, word ?? _names[midi % 12]!);

/// Đô trưởng, 80 BPM: thang âm Đô–Đô lên rồi xuống, mỗi nốt một ô nhịp (hát 3 phách, nghỉ lấy hơi
/// 1 phách). Giọng nữ hát như nốt dẫn, giọng nam hát thấp một quãng tám.
Sample doReMi() {
  const up = [60, 62, 64, 65, 67, 69, 71, 72];
  const upChords = [C, G, C, F, C, F, G, C];
  const downChords = [C, G, F, C, F, C, G, C];
  final bars = <Bar>[
    const Bar([(0, C)]),
    const Bar([(0, G)]),
    for (var i = 0; i < 8; i++) Bar([(0, upChords[i])], [held(up[i])]),
    for (var i = 0; i < 8; i++) Bar([(0, downChords[i])], [held(up[7 - i])]),
    const Bar([(0, C)]),
  ];
  return Sample(
    id: 'do-re-mi',
    title: 'Luyện giọng: Đô Rê Mi',
    bpm: 80,
    bars: bars,
    voicing: (50, 61),
    lines: const [
      LyricLine(0, 2, 'Nhạc dạo — chuẩn bị hát theo nốt dẫn'),
      LyricLine(2, 4),
      LyricLine(6, 4),
      LyricLine(10, 4),
      LyricLine(14, 4),
      LyricLine(18, 1, 'Hết bài — bấm Thu để thu lại nhé!'),
    ],
  );
}

/// La thứ, 72 BPM: chạy 5 nốt La–Mi lên, xuống (3 lần), rồi ngân dài La và Mi để luyện hơi. Giọng
/// nam hát như nốt dẫn, giọng nữ hát cao một quãng tám.
Sample laThu() {
  const phrase = [
    Bar(
      [(0, Am)],
      [
        Note(57, 0, 1, 'La'),
        Note(59, 1, 1, 'Si'),
        Note(60, 2, 1, 'Đô'),
        Note(62, 3, 1, 'Rê'),
      ],
    ),
    Bar([(0, Am)], [Note(64, 0, 3, 'Mi')]),
    Bar(
      [(0, Dm), (2, E)],
      [
        Note(62, 0, 1, 'Rê'),
        Note(60, 1, 1, 'Đô'),
        Note(59, 2, 1, 'Si'),
        Note(57, 3, 1, 'La'),
      ],
    ),
    Bar([(0, Am)], [Note(57, 0, 3, 'La')]),
  ];
  final bars = <Bar>[
    const Bar([(0, Am)]),
    const Bar([(0, E)]),
    ...phrase,
    ...phrase,
    ...phrase,
    const Bar([(0, Am)], [Note(57, 0, 7, 'La… ngân dài')]),
    const Bar([(0, F)]),
    const Bar([(0, E)], [Note(64, 0, 7, 'Mi… ngân dài')]),
    const Bar([(0, E)]),
    const Bar([(0, Am)], [Note(57, 0, 3, 'La')]),
  ];
  return Sample(
    id: 'la-thu',
    title: 'Luyện giọng: La thứ',
    bpm: 72,
    bars: bars,
    voicing: (45, 56),
    lines: const [
      LyricLine(0, 2, 'Nhạc dạo — chuẩn bị hát theo nốt dẫn'),
      LyricLine(2, 2),
      LyricLine(4, 2),
      LyricLine(6, 2),
      LyricLine(8, 2),
      LyricLine(10, 2),
      LyricLine(12, 2),
      LyricLine(14, 2),
      LyricLine(16, 2),
      LyricLine(18, 1),
    ],
  );
}

Future<void> main(List<String> args) async {
  final keepWav = args.contains('--wav');
  final root = File.fromUri(Platform.script).parent.parent.parent.path;
  final outDir = Directory('$root/app/assets/samples')
    ..createSync(recursive: true);
  final wavDir = Directory('$root/testdata/samples');
  if (keepWav) wavDir.createSync(recursive: true);
  final tmp = Directory.systemTemp.createTempSync('samples');
  try {
    for (final s in [doReMi(), laThu()]) {
      final clock = Stopwatch()..start();
      final mix = render(s);
      final wav = File('${keepWav ? wavDir.path : tmp.path}/${s.id}.wav')
        ..writeAsBytesSync(wav16(mix));
      final m4a = '${outDir.path}/${s.id}.m4a';
      final r = await Process.run('afconvert', [
        '-f', 'm4af', '-d', 'aac', '-b', '96000', '-q', '127', //
        wav.path, m4a,
      ]);
      if (r.exitCode != 0) {
        stderr.writeln('afconvert lỗi: ${r.stderr}');
        exit(1);
      }
      File('${outDir.path}/${s.id}.lrc').writeAsStringSync(lrc(s));
      stdout.writeln(
        '${s.id}: ${(mix.length / sr).toStringAsFixed(1)} s, '
        '${(File(m4a).lengthSync() / 1024).round()} KB m4a, '
        '${clock.elapsedMilliseconds} ms',
      );
    }
  } finally {
    tmp.deleteSync(recursive: true);
  }
}
