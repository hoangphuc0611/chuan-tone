import 'package:flutter/material.dart';

import '../engine/labels.dart';
import '../engine/separation_client.dart';
import 'gain_slider.dart';

/// Tách giọng ca sĩ cho bài đang nạp (M6): nút tách (chỉ cho bài gốc có giọng), tiến độ, thanh
/// "Giữ giọng ca sĩ gốc" khi đã tách.
class SeparationPanel extends StatelessWidget {
  const SeparationPanel({
    super.key,
    required this.hasServer,
    required this.hasStems,
    required this.progress,
    required this.error,
    required this.lastResult,
    required this.vocalGain,
    required this.onSeparate,
    required this.onCancel,
    required this.onDelete,
    required this.onVocalGain,
  });

  final bool hasServer;
  final bool hasStems;

  /// != null: đang tách.
  final SeparationProgress? progress;
  final String? error;
  final SeparationResult? lastResult;
  final double vocalGain;
  final VoidCallback onSeparate;
  final VoidCallback onCancel;
  final VoidCallback onDelete;
  final ValueChanged<double> onVocalGain;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final p = progress;
    final last = lastResult;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Giọng ca sĩ', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        if (p != null) ...[
          Text(separationStageLabel(p)),
          const SizedBox(height: 6),
          LinearProgressIndicator(value: p.fraction),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton(onPressed: onCancel, child: const Text('Huỷ')),
          ),
        ] else if (hasStems) ...[
          Text(
            last != null ? separationDoneLabel(last) : 'Đã tách giọng ca sĩ',
            style: theme.textTheme.bodySmall,
          ),
          GainSlider(
            label: 'Giữ giọng ca sĩ gốc',
            value: vocalGain,
            onChanged: onVocalGain,
          ),
          Text(
            vocalGain == 0
                ? 'Đang phát nhạc không lời (karaoke).'
                : 'Kéo về 0% để hát karaoke.',
            style: theme.textTheme.bodySmall,
          ),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton(
              onPressed: onDelete,
              child: const Text('Xoá bản tách'),
            ),
          ),
        ] else ...[
          FilledButton.tonalIcon(
            onPressed: hasServer ? onSeparate : null,
            icon: const Icon(Icons.record_voice_over),
            label: const Text('Tách giọng ca sĩ'),
            style: FilledButton.styleFrom(
              minimumSize: const Size.fromHeight(52),
            ),
          ),
          const SizedBox(height: 4),
          Text(
            hasServer
                ? 'Dùng cho bài gốc có giọng ca sĩ. Beat karaoke thì không cần.'
                : 'Nhập địa chỉ máy chủ tách giọng ở cuối trang.',
            style: theme.textTheme.bodySmall,
          ),
        ],
        if (error != null)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              error!,
              style: TextStyle(color: theme.colorScheme.error),
            ),
          ),
      ],
    );
  }
}

/// Hỏi trước khi gửi bài lên máy chủ (CLAUDE.md mục 7: không upload khi chưa được đồng ý).
/// Trả về null nếu không đồng ý; true/false = có chọn "Không hỏi lại".
Future<bool?> askSeparationConsent(BuildContext context) {
  var remember = false;
  return showDialog<bool>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setState) => AlertDialog(
        title: const Text('Gửi bài để tách giọng?'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Bài hát sẽ được gửi lên máy chủ ChuẩnTone chỉ để tách giọng ca sĩ khỏi nhạc. '
              'Máy chủ không nhận tên bài và tự xoá bài ngay khi tách xong (chậm nhất sau 24 giờ).',
            ),
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              value: remember,
              onChanged: (v) => setState(() => remember = v ?? false),
              title: const Text('Không hỏi lại'),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Không'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, remember),
            child: const Text('Đồng ý'),
          ),
        ],
      ),
    ),
  );
}

/// Địa chỉ máy chủ tách giọng (bản phát triển: server chạy trên Mac cùng Wi-Fi).
class ServerSettingsCard extends StatefulWidget {
  const ServerSettingsCard({
    super.key,
    required this.initial,
    required this.onSave,
  });

  final String? initial;

  /// Lưu + kiểm tra; trả câu báo kết quả.
  final Future<String> Function(String input) onSave;

  @override
  State<ServerSettingsCard> createState() => _ServerSettingsCardState();
}

class _ServerSettingsCardState extends State<ServerSettingsCard> {
  late final _controller = TextEditingController(text: widget.initial ?? '');
  String? _status;
  bool _busy = false;

  @override
  void didUpdateWidget(covariant ServerSettingsCard old) {
    super.didUpdateWidget(old);
    if (old.initial != widget.initial && _controller.text.isEmpty) {
      _controller.text = widget.initial ?? '';
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    setState(() => _busy = true);
    final status = await widget.onSave(_controller.text);
    if (mounted) {
      setState(() {
        _busy = false;
        _status = status;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Máy chủ tách giọng',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            TextField(
              controller: _controller,
              keyboardType: TextInputType.url,
              autocorrect: false,
              decoration: const InputDecoration(
                hintText: '192.168.1.10:8000',
                helperText: 'Địa chỉ server in ra khi chạy "uv run chuantone-server" trên Mac',
              ),
              onSubmitted: (_) => _save(),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                FilledButton(
                  onPressed: _busy ? null : _save,
                  child: const Text('Lưu & kiểm tra'),
                ),
                const SizedBox(width: 12),
                if (_busy)
                  const SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                if (!_busy && _status != null) Expanded(child: Text(_status!)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
