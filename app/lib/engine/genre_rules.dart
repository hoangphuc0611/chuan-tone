import 'package:tonecore_flutter/tonecore_flutter.dart';

/// Mức tự chỉnh giọng đi kèm một thể loại (cột "Chỉnh giọng", bảng mục 6 của spec;
/// docs/m5-plan.md điểm 5). Chọn preset thì đặt luôn mức này; sau đó người dùng bấm nút chỉnh
/// giọng nào thì giữ nút đó cho tới khi chọn preset khác.
class GenreCorrection {
  /// Bật mức [level], thời gian kéo về nốt [retuneMs] (thay cho giá trị của mức).
  const GenreCorrection(CorrectionPreset this.level, double this.retuneMs)
    : keep = false;

  /// Tắt tự chỉnh giọng (Rap; Robot vẫn bấm được).
  const GenreCorrection.off() : keep = false, level = null, retuneMs = null;

  /// Giữ nguyên lựa chọn hiện tại ("Khô").
  const GenreCorrection.keep() : keep = true, level = null, retuneMs = null;

  final bool keep;

  /// null (khi không [keep]) = tắt.
  final CorrectionPreset? level;
  final double? retuneMs;
}

GenreCorrection genreCorrection(Genre g) => switch (g) {
  Genre.dry => const GenreCorrection.keep(),
  Genre.bolero => const GenreCorrection(CorrectionPreset.natural, 45),
  Genre.ballad => const GenreCorrection(CorrectionPreset.natural, 35),
  Genre.pop => const GenreCorrection(CorrectionPreset.singer, 15),
  Genre.remix => const GenreCorrection(CorrectionPreset.singer, 8),
  Genre.rap => const GenreCorrection.off(),
  Genre.lofi => const GenreCorrection(CorrectionPreset.natural, 30),
};

/// Preset đang dùng có từ đâu.
enum GenreSource {
  /// Người dùng đã chọn cho bài này (nhớ theo bài).
  saved,

  /// Tên file có tên thể loại.
  name,

  /// Theo nhịp bài (BPM từ phân tích).
  bpm,
}

typedef GenreSuggestion = ({Genre genre, GenreSource source});

/// Gợi ý preset cho bài (docs/m5-plan.md điểm 6): đã chọn cho bài → tên file → BPM. null = không
/// đoán được → giữ preset người dùng chọn lần trước.
GenreSuggestion? suggestGenre({
  Genre? saved,
  required String fileName,
  double? bpm,
}) {
  if (saved != null) return (genre: saved, source: GenreSource.saved);
  final byName = genreFromName(fileName);
  if (byName != null) return (genre: byName, source: GenreSource.name);
  final byBpm = genreFromBpm(bpm);
  if (byBpm != null) return (genre: byBpm, source: GenreSource.bpm);
  return null;
}

// Thứ tự ưu tiên khi tên có nhiều từ khoá: kiểu phối (lofi, remix) quyết định beat nghe ra sao nên
// đứng trước thể loại bài ("Bolero Remix" → Remix).
const _nameRules = <(Genre, List<String>)>[
  (Genre.lofi, ['lofi', 'lo fi']),
  (Genre.remix, ['remix', 'rmx', 'vinahouse', 'edm']),
  (Genre.rap, ['rap', 'hiphop', 'hip hop']),
  (Genre.bolero, ['bolero', 'tru tinh', 'nhac vang']),
  (Genre.ballad, ['ballad']),
];

/// Thể loại theo từ khoá trong tên file (không phân biệt hoa thường / dấu tiếng Việt, khớp nguyên
/// từ: "Trap" hay "Grape" không phải "rap").
Genre? genreFromName(String fileName) {
  final dot = fileName.lastIndexOf('.');
  final stem = dot > 0 ? fileName.substring(0, dot) : fileName;
  final words =
      ' ${foldVietnamese(stem).replaceAll(RegExp('[^a-z0-9]+'), ' ')} ';
  for (final (genre, keys) in _nameRules) {
    if (keys.any((k) => words.contains(' $k '))) return genre;
  }
  return null;
}

/// Nhanh (≥ 118 BPM) → Nhạc trẻ; chậm (≤ 80 BPM) → Ballad; ở giữa → không đoán.
Genre? genreFromBpm(double? bpm) {
  if (bpm == null || !bpm.isFinite || bpm <= 0) return null;
  if (bpm >= 118) return Genre.pop;
  if (bpm <= 80) return Genre.ballad;
  return null;
}

const _foldGroups = {
  'a': 'àáạảãâầấậẩẫăằắặẳẵ',
  'e': 'èéẹẻẽêềếệểễ',
  'i': 'ìíịỉĩ',
  'o': 'òóọỏõôồốộổỗơờớợởỡ',
  'u': 'ùúụủũưừứựửữ',
  'y': 'ỳýỵỷỹ',
  'd': 'đ',
};

final Map<int, String> _foldMap = {
  for (final MapEntry(key: base, value: chars) in _foldGroups.entries)
    for (final c in chars.runes) c: base,
};

/// Chữ thường, bỏ dấu tiếng Việt ("Trữ Tình" → "tru tinh"). Nhận cả dạng dấu rời (NFD — tên file
/// trên iOS / macOS thường ở dạng này): bỏ các dấu kết hợp U+0300…U+036F.
String foldVietnamese(String s) {
  final out = StringBuffer();
  for (final r in s.toLowerCase().runes) {
    if (r >= 0x300 && r <= 0x36f) continue;
    out.write(_foldMap[r] ?? String.fromCharCode(r));
  }
  return out.toString();
}
