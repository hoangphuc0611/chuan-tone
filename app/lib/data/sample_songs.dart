import 'dart:io';

import 'package:flutter/services.dart';

import 'song_library.dart';

/// Bài mẫu đóng gói trong app (M8 B3, docs/m8-plan.md điểm 2): bài luyện giọng do
/// `tools/samples/make_samples.dart` tạo bằng code — nhạc của dự án, không vướng bản quyền — kèm
/// lời `.lrc` có mốc từng chữ.
class SampleSong {
  const SampleSong({
    required this.id,
    required this.title,
    required this.subtitle,
  });

  final String id;
  final String title;

  /// Mô tả ngắn ở Trang chủ.
  final String subtitle;

  String get audioAsset => 'assets/samples/$id.m4a';
  String get lyricsAsset => 'assets/samples/$id.lrc';

  /// Tên "file" của bài trong thư viện (một nửa khoá bài; tên hiển thị = bỏ đuôi).
  String get fileName => '$title.m4a';
}

const sampleSongs = [
  SampleSong(
    id: 'do-re-mi',
    title: 'Luyện giọng: Đô Rê Mi',
    subtitle: 'Đô trưởng · hát theo nốt dẫn · 1 phút',
  ),
  SampleSong(
    id: 'la-thu',
    title: 'Luyện giọng: La thứ',
    subtitle: 'La thứ · chạy nốt và ngân dài · 1 phút',
  ),
];

/// Đưa bài mẫu vào thư viện (lần đầu bấm) kèm lời; đã có thì trả bài đó (lời mất thì nạp lại).
Future<LibrarySong> installSample(
  SongLibrary library,
  SampleSong s, {
  AssetBundle? bundle,
}) async {
  final b = bundle ?? rootBundle;
  final audio = await b.load(s.audioAsset);
  final existing = await library.find(s.fileName, audio.lengthInBytes);
  if (existing != null && !existing.missing && existing.hasLyrics) {
    await library.touch(existing.name, existing.size, sung: false);
    return existing;
  }
  final tmp = await Directory.systemTemp.createTemp('bai_mau');
  try {
    final song = existing != null && !existing.missing
        ? existing
        : await library.add(
            (await File('${tmp.path}/${s.id}.m4a').writeAsBytes(
              audio.buffer.asUint8List(
                audio.offsetInBytes,
                audio.lengthInBytes,
              ),
            )).path,
            name: s.fileName,
            move: true,
            sample: true,
          );
    final lrc = File('${tmp.path}/${s.id}.lrc');
    await lrc.writeAsString(await b.loadString(s.lyricsAsset));
    await library.setLyrics(song.name, song.size, lrc.path);
    return (await library.find(song.name, song.size))!;
  } finally {
    await tmp.delete(recursive: true);
  }
}
