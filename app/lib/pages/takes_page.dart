import 'package:flutter/material.dart';

import '../data/take_store.dart';
import '../data/test_log.dart';
import '../engine/labels.dart';
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
    return Scaffold(
      appBar: AppBar(title: const Text('Bản thu của tôi')),
      body: takes == null
          ? const Center(child: CircularProgressIndicator())
          : takes.isEmpty
          ? const Center(
              child: Padding(
                padding: EdgeInsets.all(24),
                child: Text(
                  'Chưa có bản thu. Vào Phòng thử, chọn bài rồi bấm "Thu âm".',
                  textAlign: TextAlign.center,
                ),
              ),
            )
          : ListView.separated(
              itemCount: takes.length,
              separatorBuilder: (_, _) => const Divider(height: 1),
              itemBuilder: (context, i) {
                final t = takes[i];
                return ListTile(
                  leading: const Icon(Icons.graphic_eq),
                  title: Text(
                    songTitle(t.info.songName),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                  subtitle: Text(
                    '${formatTakeDate(t.info.createdAt)} · '
                    '${formatDuration(t.info.duration)} · ${formatBytes(t.sizeBytes())}',
                    style: theme.textTheme.bodySmall,
                  ),
                  onTap: () => _open(t),
                  trailing: IconButton(
                    tooltip: 'Xoá',
                    icon: const Icon(Icons.delete_outline),
                    onPressed: () => _delete(t),
                  ),
                );
              },
            ),
    );
  }
}
