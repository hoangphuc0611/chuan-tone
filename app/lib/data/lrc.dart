import 'dart:convert';

/// Một chữ có mốc thời gian (LRC mở rộng: `<mm:ss.xx>chữ`).
class LyricWord {
  const LyricWord(this.start, this.offset);

  final Duration start;

  /// Vị trí ký tự đầu của chữ trong [LyricLine.text].
  final int offset;
}

/// Một dòng lời: mốc bắt đầu, chữ (đã bỏ thẻ), mốc từng chữ nếu có.
class LyricLine {
  const LyricLine({
    required this.start,
    required this.text,
    this.words = const [],
    this.end,
  });

  final Duration start;
  final String text;

  /// Rỗng = chỉ có mốc dòng.
  final List<LyricWord> words;

  /// Mốc kết thúc chữ cuối (thẻ chữ cuối dòng không kèm chữ), null nếu không có.
  final Duration? end;
}

/// Lời bài hát đã đọc từ file `.lrc` (M8 B3). Các dòng xếp theo thời gian, đã áp thẻ `[offset:]`.
class Lyrics {
  const Lyrics({required this.lines, this.title, this.artist});

  final List<LyricLine> lines;
  final String? title;
  final String? artist;

  /// Dòng đang hát ở thời điểm [t] (−1 = chưa tới dòng đầu).
  int indexAt(Duration t) {
    var lo = 0;
    var hi = lines.length - 1;
    var found = -1;
    while (lo <= hi) {
      final mid = (lo + hi) >> 1;
      if (lines[mid].start <= t) {
        found = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    return found;
  }

  /// Số ký tự đầu của dòng [i] đã hát tới thời điểm [t] (để tô màu kiểu karaoke).
  ///
  /// Có mốc từng chữ: các chữ đã qua tô hết, chữ đang hát tô dần tới mốc chữ sau. Chỉ có mốc dòng:
  /// tô dần cả dòng trong khoảng tới dòng sau, nhưng không chậm hơn ~0,35 s mỗi ký tự (dòng đứng
  /// trước một đoạn nhạc dạo dài vẫn tô xong khi hát hết câu).
  int sungChars(int i, Duration t) {
    if (i < 0 || i >= lines.length) return 0;
    final line = lines[i];
    final len = line.text.length;
    if (len == 0 || t < line.start) return 0;
    final nextStart = i + 1 < lines.length ? lines[i + 1].start : null;
    final words = line.words;
    if (words.isEmpty) {
      final cap = Duration(milliseconds: 500 + 350 * len);
      var span = line.end != null ? line.end! - line.start : cap;
      if (nextStart != null && nextStart - line.start < span) {
        span = nextStart - line.start;
      }
      return _cut(line.text, _fraction(t - line.start, span) * len);
    }
    var k = -1;
    for (var w = 0; w < words.length && words[w].start <= t; w++) {
      k = w;
    }
    if (k < 0) return _cut(line.text, words.first.offset.toDouble());
    final from = words[k].offset;
    final to = k + 1 < words.length ? words[k + 1].offset : len;
    final until = k + 1 < words.length
        ? words[k + 1].start
        : line.end ?? nextStart ?? words[k].start + const Duration(seconds: 1);
    final cap = Duration(milliseconds: 300 + 400 * (to - from));
    var span = until - words[k].start;
    if (span > cap) span = cap;
    return _cut(
      line.text,
      from + _fraction(t - words[k].start, span) * (to - from),
    );
  }

  static double _fraction(Duration elapsed, Duration span) {
    if (span <= Duration.zero) return 1;
    return (elapsed.inMicroseconds / span.inMicroseconds).clamp(0.0, 1.0);
  }

  /// Làm tròn số ký tự, không cắt giữa chữ cái và dấu rời đi theo nó (U+0300…U+036F).
  static int _cut(String text, double chars) {
    var n = chars.round().clamp(0, text.length);
    while (n < text.length && _isMark(text.codeUnitAt(n))) {
      n++;
    }
    return n;
  }

  static bool _isMark(int c) => c >= 0x0300 && c <= 0x036F;
}

/// Đọc file lời từ byte: UTF-8 (có / không BOM), UTF-16 có BOM; byte UTF-8 hỏng thay bằng �.
Lyrics decodeLrc(List<int> bytes) {
  String text;
  if (bytes.length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    text = _utf16(bytes.sublist(2), littleEndian: true);
  } else if (bytes.length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    text = _utf16(bytes.sublist(2), littleEndian: false);
  } else {
    text = utf8.decode(bytes, allowMalformed: true);
  }
  return parseLrc(text);
}

String _utf16(List<int> b, {required bool littleEndian}) {
  final units = <int>[];
  for (var i = 0; i + 1 < b.length; i += 2) {
    units.add(littleEndian ? b[i] | (b[i + 1] << 8) : (b[i] << 8) | b[i + 1]);
  }
  return String.fromCharCodes(units);
}

final _stamp = RegExp(r'\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\]');
final _anyStamp = RegExp(r'[\[<](\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?[\]>]');
final _tag = RegExp(r'^\[([a-zA-Z#]+):(.*)\]$');

/// Đọc lời dạng LRC (tự viết theo mô tả định dạng công khai):
/// - dòng `[mm:ss.xx]lời`, phần lẻ 1–3 chữ số, nhiều mốc trên một dòng (điệp khúc lặp lại);
/// - mốc từng chữ `<mm:ss.xx>` (hoặc `[mm:ss.xx]` giữa dòng);
/// - thẻ `[ti:]`, `[ar:]`, `[offset:±ms]` (dương = lời hiện sớm hơn); thẻ khác bỏ qua;
/// - dòng không có mốc bỏ qua; dòng có mốc mà rỗng giữ lại (đoạn nhạc không lời).
///
/// Không có dòng nào có mốc → [FormatException].
Lyrics parseLrc(String source) {
  var text = source;
  if (text.startsWith('﻿')) text = text.substring(1);
  String? title;
  String? artist;
  var offsetMs = 0;
  final raw = <LyricLine>[];
  for (final rawLine in const LineSplitter().convert(text)) {
    final line = rawLine.trim();
    if (line.isEmpty) continue;
    final tag = _tag.firstMatch(line);
    if (tag != null && !_stamp.hasMatch(line)) {
      final value = tag.group(2)!.trim();
      switch (tag.group(1)!.toLowerCase()) {
        case 'ti':
          if (value.isNotEmpty) title = value;
        case 'ar':
          if (value.isNotEmpty) artist = value;
        case 'offset':
          offsetMs = int.tryParse(value.replaceFirst('+', '')) ?? 0;
      }
      continue;
    }
    // Các mốc đầu dòng.
    final starts = <Duration>[];
    var pos = 0;
    while (true) {
      final m = _stamp.matchAsPrefix(line, pos);
      if (m == null) break;
      starts.add(_duration(m));
      pos = m.end;
    }
    if (starts.isEmpty) continue;
    final (body, words, end) = _words(line.substring(pos), starts.first);
    for (final s in starts) {
      final shift = s - starts.first;
      raw.add(
        LyricLine(
          start: s,
          text: body,
          words: [for (final w in words) LyricWord(w.start + shift, w.offset)],
          end: end == null ? null : end + shift,
        ),
      );
    }
  }
  if (raw.isEmpty) {
    throw const FormatException('Không có dòng lời nào có mốc thời gian');
  }
  final shift = Duration(milliseconds: offsetMs);
  final lines = [
    for (final l in raw)
      LyricLine(
        start: _clamp(l.start - shift),
        text: l.text,
        words: [
          for (final w in l.words) LyricWord(_clamp(w.start - shift), w.offset),
        ],
        end: l.end == null ? null : _clamp(l.end! - shift),
      ),
  ];
  // Sắp xếp ổn định theo mốc (dòng cùng mốc giữ thứ tự trong file).
  final indexed = [for (var i = 0; i < lines.length; i++) (i, lines[i])];
  indexed.sort((a, b) {
    final c = a.$2.start.compareTo(b.$2.start);
    return c != 0 ? c : a.$1.compareTo(b.$1);
  });
  return Lyrics(
    lines: [for (final (_, l) in indexed) l],
    title: title,
    artist: artist,
  );
}

/// Tách phần lời sau các mốc đầu dòng thành chữ + mốc từng chữ.
(String, List<LyricWord>, Duration?) _words(String rest, Duration lineStart) {
  if (!_anyStamp.hasMatch(rest)) return (rest.trim(), const [], null);
  final buf = StringBuffer();
  final words = <LyricWord>[];
  Duration? pendingStart;
  Duration? end;
  var pos = 0;
  for (final m in _anyStamp.allMatches(rest)) {
    final seg = rest.substring(pos, m.start);
    if (seg.isNotEmpty) {
      words.add(LyricWord(pendingStart ?? lineStart, buf.length));
      buf.write(seg);
    }
    pendingStart = _duration(m);
    pos = m.end;
  }
  final tail = rest.substring(pos);
  if (tail.isNotEmpty) {
    words.add(LyricWord(pendingStart ?? lineStart, buf.length));
    buf.write(tail);
  } else {
    end = pendingStart; // mốc cuối không kèm chữ = lúc hát xong chữ cuối
  }
  // Bỏ khoảng trắng đầu / cuối, dời vị trí chữ theo.
  final full = buf.toString();
  final lead = full.length - full.trimLeft().length;
  final body = full.trim();
  final out = <LyricWord>[];
  for (final w in words) {
    final o = (w.offset - lead).clamp(0, body.length);
    // Chữ chỉ gồm khoảng trắng ở đầu / cuối dòng: bỏ.
    if (o >= body.length && body.isNotEmpty) continue;
    if (out.isNotEmpty && out.last.offset == o) {
      out[out.length - 1] = LyricWord(w.start, o);
    } else {
      out.add(LyricWord(w.start, o));
    }
  }
  return (body, body.isEmpty ? const [] : out, end);
}

Duration _duration(Match m) {
  final min = int.parse(m.group(1)!);
  final sec = int.parse(m.group(2)!);
  final frac = m.group(3);
  final ms = frac == null
      ? 0
      : int.parse(frac) * const [100, 10, 1][frac.length - 1];
  return Duration(minutes: min, seconds: sec, milliseconds: ms);
}

Duration _clamp(Duration d) => d < Duration.zero ? Duration.zero : d;

/// Chuỗi lệch lời cho người dùng: "Lời sớm hơn 0,5 s" / "Lời đúng như file" / "Lời muộn hơn 1 s".
String lyricsOffsetLabel(int ms) {
  if (ms == 0) return 'Lời đúng như file';
  final s = (ms.abs() / 1000)
      .toStringAsFixed(ms.abs() % 1000 == 0 ? 0 : 1)
      .replaceAll('.', ',');
  return ms > 0 ? 'Lời sớm hơn $s s' : 'Lời muộn hơn $s s';
}
