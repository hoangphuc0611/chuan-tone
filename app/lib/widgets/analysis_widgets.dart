import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/labels.dart';

enum StepStatus { waiting, running, done, skipped, failed }

class AnalysisStep {
  const AnalysisStep({
    required this.title,
    required this.state,
    this.subtitle,
    this.progress,
    this.action,
  });

  final String title;
  final StepStatus state;
  final String? subtitle;

  /// 0..1 khi đang chạy (null = không rõ).
  final double? progress;

  /// Nút bên phải (vd. "Tách" ở bước tách giọng).
  final Widget? action;
}

/// Các bước phân tích bài (spec mục 5 màn 2: tách vocal → dò tone → dò BPM).
class AnalysisSteps extends StatelessWidget {
  const AnalysisSteps({super.key, required this.steps});

  final List<AnalysisStep> steps;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      children: [
        for (final s in steps)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 6),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SizedBox(width: 32, height: 32, child: _icon(s, theme)),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(s.title, style: theme.textTheme.titleMedium),
                      if (s.subtitle != null)
                        Text(
                          s.subtitle!,
                          style: theme.textTheme.bodySmall?.copyWith(
                            color: s.state == StepStatus.failed
                                ? theme.colorScheme.error
                                : null,
                          ),
                        ),
                      if (s.state == StepStatus.running)
                        Padding(
                          padding: const EdgeInsets.only(top: 6),
                          child: LinearProgressIndicator(value: s.progress),
                        ),
                    ],
                  ),
                ),
                if (s.action != null) ...[const SizedBox(width: 8), s.action!],
              ],
            ),
          ),
      ],
    );
  }

  static Widget _icon(AnalysisStep s, ThemeData theme) => switch (s.state) {
    StepStatus.waiting => Icon(
      Icons.radio_button_unchecked,
      color: theme.colorScheme.outline,
    ),
    StepStatus.running => const Padding(
      padding: EdgeInsets.all(6),
      child: CircularProgressIndicator(strokeWidth: 2.5),
    ),
    StepStatus.done => Icon(
      Icons.check_circle,
      color: theme.colorScheme.primary,
    ),
    StepStatus.skipped => Icon(
      Icons.remove_circle_outline,
      color: theme.colorScheme.outline,
    ),
    StepStatus.failed => Icon(Icons.error, color: theme.colorScheme.error),
  };
}

/// "Tone: La thứ (Am) – 92 BPM" thật to + chuyển tone, tuning, nút sửa tone.
class KeyResultCard extends StatelessWidget {
  const KeyResultCard({
    super.key,
    required this.analysis,
    required this.manualKey,
    required this.onEdit,
  });

  final SongAnalysis analysis;

  /// Tone người dùng sửa tay (null = theo kết quả dò).
  final MusicKey? manualKey;
  final VoidCallback onEdit;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final key = manualKey ?? analysis.key;
    final bpm = analysis.bpm;
    final modulation = manualKey == null ? modulationSummary(analysis) : null;
    final tuning = tuningNote(analysis);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              key == null ? 'Chưa dò được tone' : 'Tone: ${keyLabel(key)}',
              style: theme.textTheme.headlineSmall?.copyWith(
                fontWeight: FontWeight.w700,
              ),
            ),
            if (bpm != null)
              Text('${bpm.round()} BPM', style: theme.textTheme.titleMedium),
            if (manualKey != null)
              Text(
                'Đã sửa tay (máy dò: ${analysis.key == null ? 'không rõ' : keyLabel(analysis.key!)})',
                style: theme.textTheme.bodySmall,
              ),
            if (modulation != null) Text('Chuyển tone: $modulation'),
            if (tuning != null) Text(tuning, style: theme.textTheme.bodySmall),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: onEdit,
              icon: const Icon(Icons.edit),
              label: const Text('Sửa tone'),
            ),
          ],
        ),
      ),
    );
  }
}

/// Kết quả hộp sửa tone: [key] mới, hoặc [reset] = quay về tone máy dò.
class KeyEdit {
  const KeyEdit.set(MusicKey this.key) : reset = false;
  const KeyEdit.reset() : key = null, reset = true;

  final MusicKey? key;
  final bool reset;
}

/// Chọn chủ âm (12 nốt) + trưởng / thứ. null = huỷ.
Future<KeyEdit?> showKeyEditor(
  BuildContext context, {
  required MusicKey initial,
  required bool canReset,
}) {
  var root = initial.root;
  var minor =
      initial.scale == MusicScale.naturalMinor ||
      initial.scale == MusicScale.harmonicMinor ||
      initial.scale == MusicScale.pentatonicMinor;
  return showDialog<KeyEdit>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setState) {
        final key = MusicKey(
          root,
          minor ? MusicScale.naturalMinor : MusicScale.major,
        );
        return AlertDialog(
          title: const Text('Sửa tone của bài'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              DropdownButton<int>(
                value: root,
                isExpanded: true,
                items: [
                  for (var r = 0; r < 12; r++)
                    DropdownMenuItem(
                      value: r,
                      child: Text('${noteNameVi(r)} (${noteLetter(r)})'),
                    ),
                ],
                onChanged: (r) => setState(() => root = r ?? root),
              ),
              const SizedBox(height: 8),
              SegmentedButton<bool>(
                segments: const [
                  ButtonSegment(value: false, label: Text('Trưởng')),
                  ButtonSegment(value: true, label: Text('Thứ')),
                ],
                selected: {minor},
                showSelectedIcon: false,
                onSelectionChanged: (v) => setState(() => minor = v.single),
              ),
              const SizedBox(height: 12),
              Text(keyLabel(key), style: Theme.of(ctx).textTheme.titleMedium),
            ],
          ),
          actions: [
            if (canReset)
              TextButton(
                onPressed: () => Navigator.pop(ctx, const KeyEdit.reset()),
                child: const Text('Theo máy dò'),
              ),
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Huỷ'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(ctx, KeyEdit.set(key)),
              child: const Text('Lưu'),
            ),
          ],
        );
      },
    ),
  );
}
