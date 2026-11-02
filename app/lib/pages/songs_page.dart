import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/song_import.dart';
import '../data/song_library.dart';
import '../engine/labels.dart';
import 'analysis_page.dart';

/// "Bài hát của tôi" (M8 B1): thư viện bài, mới dùng nhất trước; tìm theo tên (gõ không dấu cũng
/// được), thêm bài, đổi tên, xoá. Bấm bài → màn Phân tích bài (kết quả đã lưu hiện ngay, có nút
/// "Hát").
class SongsPage extends StatefulWidget {
  const SongsPage({super.key, this.library, this.engineVersion, this.onOpen});

  final SongLibrary? library;

  /// Phiên bản engine để lấy kết quả phân tích đã lưu (null = engine đang chạy).
  final String? engineVersion;

  /// Mở bài (mặc định: màn Phân tích bài); test thay bằng hàm giả.
  final Future<void> Function(BuildContext context, LibrarySong song)? onOpen;

  @override
  State<SongsPage> createState() => _SongsPageState();
}

class _SongsPageState extends State<SongsPage> {
  late final SongLibrary _library = widget.library ?? SongLibrary();
  late final String _engineVersion = widget.engineVersion ?? ToneCore.version;
  final _search = TextEditingController();
  List<LibrarySong>? _all;

  @override
  void initState() {
    super.initState();
    _reload();
  }

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  Future<void> _reload() async {
    final all = await _library.songs(engineVersion: _engineVersion);
    if (mounted) setState(() => _all = all);
  }

  Future<void> _open(LibrarySong song) async {
    if (song.missing) {
      _snack(
        'File nhạc của bài này không còn trên máy. Hãy xoá rồi thêm lại bài.',
      );
      return;
    }
    final open = widget.onOpen;
    if (open != null) {
      await open(context, song);
    } else {
      await Navigator.of(context).push(
        MaterialPageRoute<void>(builder: (_) => AnalysisPage(song: song.ref)),
      );
    }
    await _reload();
  }

  Future<void> _add() async {
    try {
      final song = await pickSongIntoLibrary(_library);
      if (song == null || !mounted) return;
      await _open(song);
    } catch (_) {
      _snack('Không thêm được file này. Hãy thử file MP3, M4A hoặc WAV.');
    }
  }

  Future<void> _rename(LibrarySong song) async {
    final title = await showDialog<String>(
      context: context,
      builder: (_) => _RenameDialog(initial: song.title),
    );
    if (title == null) return;
    await _library.rename(song, title);
    await _reload();
  }

  Future<void> _delete(LibrarySong song) async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Xoá bài "${song.title}"?'),
        content: const Text(
          'File nhạc, giọng ca sĩ đã tách và kết quả dò tone của bài sẽ bị xoá. '
          'Bản thu của bạn vẫn được giữ.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Huỷ'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Xoá'),
          ),
        ],
      ),
    );
    if (yes != true) return;
    await _library.delete(song);
    await _reload();
  }

  void _snack(String text) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(text)));
  }

  @override
  Widget build(BuildContext context) {
    final all = _all;
    final songs = all == null
        ? null
        : [
            for (final s in all)
              if (songMatches(s, _search.text)) s,
          ];
    return Scaffold(
      appBar: AppBar(title: const Text('Bài hát của tôi')),
      floatingActionButton: all == null || all.isEmpty
          ? null
          : FloatingActionButton.extended(
              onPressed: _add,
              icon: const Icon(Icons.add),
              label: const Text('Thêm bài'),
            ),
      body: songs == null
          ? const Center(child: CircularProgressIndicator())
          : all!.isEmpty
          ? _EmptyLibrary(onAdd: _add)
          : Column(
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(16, 8, 16, 8),
                  child: TextField(
                    controller: _search,
                    onChanged: (_) => setState(() {}),
                    textInputAction: TextInputAction.search,
                    decoration: InputDecoration(
                      prefixIcon: const Icon(Icons.search),
                      hintText: 'Tìm bài (gõ không dấu cũng được)',
                      border: const OutlineInputBorder(),
                      isDense: true,
                      suffixIcon: _search.text.isEmpty
                          ? null
                          : IconButton(
                              tooltip: 'Xoá chữ',
                              icon: const Icon(Icons.close),
                              onPressed: () => setState(_search.clear),
                            ),
                    ),
                  ),
                ),
                Expanded(
                  child: songs.isEmpty
                      ? const Center(child: Text('Không có bài nào khớp.'))
                      : ListView.separated(
                          padding: const EdgeInsets.only(bottom: 88),
                          itemCount: songs.length,
                          separatorBuilder: (_, _) => const Divider(height: 1),
                          itemBuilder: (context, i) => _SongTile(
                            song: songs[i],
                            onTap: () => _open(songs[i]),
                            onRename: () => _rename(songs[i]),
                            onDelete: () => _delete(songs[i]),
                          ),
                        ),
                ),
              ],
            ),
    );
  }
}

/// Dòng phụ của một bài: "Sol thứ (Gm) · 85 BPM · tone +2".
String songSubtitle(LibrarySong s) {
  final parts = <String>[];
  final k = s.key0;
  if (k != null) parts.add(keyLabel(k));
  final bpm = s.analysis?.bpm;
  if (bpm != null) parts.add('${bpm.round()} BPM');
  if (s.transpose != 0) parts.add('tone ${transposeLabel(s.transpose)}');
  if (s.missing) return 'File nhạc không còn trên máy';
  return parts.isEmpty ? 'Chưa dò tone' : parts.join(' · ');
}

class _SongTile extends StatelessWidget {
  const _SongTile({
    required this.song,
    required this.onTap,
    required this.onRename,
    required this.onDelete,
  });

  final LibrarySong song;
  final VoidCallback onTap;
  final VoidCallback onRename;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ListTile(
      minVerticalPadding: 12,
      leading: Icon(
        song.missing ? Icons.music_off : Icons.music_note,
        color: song.missing ? theme.colorScheme.error : null,
      ),
      title: Text(song.title, maxLines: 2, overflow: TextOverflow.ellipsis),
      subtitle: Text(songSubtitle(song)),
      onTap: onTap,
      trailing: PopupMenuButton<String>(
        tooltip: 'Tuỳ chọn',
        onSelected: (v) => v == 'rename' ? onRename() : onDelete(),
        itemBuilder: (_) => const [
          PopupMenuItem(value: 'rename', child: Text('Đổi tên')),
          PopupMenuItem(value: 'delete', child: Text('Xoá bài')),
        ],
      ),
    );
  }
}

/// Hộp đổi tên bài (giữ bộ điều khiển chữ tới khi hộp đóng hẳn).
class _RenameDialog extends StatefulWidget {
  const _RenameDialog({required this.initial});

  final String initial;

  @override
  State<_RenameDialog> createState() => _RenameDialogState();
}

class _RenameDialogState extends State<_RenameDialog> {
  late final _ctl = TextEditingController(text: widget.initial);

  @override
  void dispose() {
    _ctl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('Đổi tên bài'),
    content: TextField(
      controller: _ctl,
      autofocus: true,
      textCapitalization: TextCapitalization.sentences,
      decoration: const InputDecoration(hintText: 'Tên bài'),
      onSubmitted: (v) => Navigator.pop(context, v),
    ),
    actions: [
      TextButton(
        onPressed: () => Navigator.pop(context),
        child: const Text('Huỷ'),
      ),
      FilledButton(
        onPressed: () => Navigator.pop(context, _ctl.text),
        child: const Text('Lưu'),
      ),
    ],
  );
}

class _EmptyLibrary extends StatelessWidget {
  const _EmptyLibrary({required this.onAdd});

  final VoidCallback onAdd;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.library_music,
              size: 64,
              color: theme.colorScheme.primary,
            ),
            const SizedBox(height: 16),
            Text('Chưa có bài nào', style: theme.textTheme.titleLarge),
            const SizedBox(height: 8),
            const Text(
              'Thêm file nhạc từ app Tệp, hoặc mở file nhạc trong Zalo, Safari… '
              'rồi chọn "ChuẩnTone".',
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 24),
            FilledButton.icon(
              onPressed: onAdd,
              icon: const Icon(Icons.add),
              label: const Text('Thêm bài hát'),
              style: FilledButton.styleFrom(minimumSize: const Size(220, 56)),
            ),
          ],
        ),
      ),
    );
  }
}
