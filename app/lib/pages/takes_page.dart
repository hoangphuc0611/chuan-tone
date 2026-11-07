import 'package:flutter/material.dart';

import '../data/take_store.dart';
import '../data/test_log.dart';
import '../engine/labels.dart';
import '../widgets/app_ui.dart';
import 'review_page.dart';

/// "Bản thu của tôi" (M7): các bản thu trên máy, mới nhất trước; bấm để nghe lại & xuất.
class TakesPage extends StatefulWidget {
  const TakesPage({super.key, this.store});

  final TakeStore? store;

  @override
  State<TakesPage> createState() => _TakesPageState();
}

class _TakesPageState extends State<TakesPage> {
  late final TakeStore _store = widget.store ?? TakeStore();
  List<Take>? _takes;

  @override
  void initState() {
    super.initState();
    TestLog.screen('takes');
    _reload();
  }

  Future<void> _reload() async {
    final t = await _store.list();
    if (mounted) setState(() => _takes = t);
  }

  Future<void> _open(Take t) async {
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => ReviewPage(take: t, store: _store),
      ),
    );
    await _reload();
  }

  Future<void> _delete(Take t) async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Xoá bản thu?'),
        content: Text(
          '${songTitle(t.info.songName)} · ${formatTakeDate(t.info.createdAt)}',
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
    await _store.delete(t);
    await _reload();
  }

  @override
  Widget build(BuildContext context) {
    final takes = _takes;
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    return Scaffold(
      appBar: AppBar(title: const Text('Bản thu của tôi')),
      body: takes == null
          ? const Center(child: CircularProgressIndicator())
          : takes.isEmpty
          ? const Center(
              child: SingleChildScrollView(
                padding: EdgeInsets.all(16),
                child: EmptyState(
                  icon: Icons.graphic_eq_rounded,
                  title: 'Chưa có bản thu nào',
                  body:
                      'Vào một bài và bấm Thu. Thu xong, bản thu sẽ nằm ở đây.',
                ),
              ),
            )
          : ListView.separated(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
              itemCount: takes.length,
              separatorBuilder: (_, _) => const SizedBox(height: 8),
              itemBuilder: (context, i) {
                final t = takes[i];
                return Card(
                  clipBehavior: Clip.antiAlias,
                  child: ListTile(
                    minVerticalPadding: 12,
                    contentPadding: const EdgeInsets.only(left: 12, right: 4),
                    leading: IconBadge(
                      t.videoPath != null
                          ? Icons.videocam_rounded
                          : Icons.graphic_eq_rounded,
                    ),
                    title: Text(
                      songTitle(t.info.songName),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: theme.textTheme.titleMedium,
                    ),
                    subtitle: Text(
                      '${formatTakeDate(t.info.createdAt)} · '
                      '${formatDuration(t.info.duration)} · ${formatBytes(t.sizeBytes())}',
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: cs.onSurfaceVariant,
                      ),
                    ),
                    onTap: () => _open(t),
                    trailing: IconButton(
                      tooltip: 'Xoá',
                      icon: const Icon(Icons.delete_outline),
                      onPressed: () => _delete(t),
                    ),
                  ),
                );
              },
            ),
    );
  }
}
