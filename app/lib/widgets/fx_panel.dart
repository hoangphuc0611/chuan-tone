import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/genre_rules.dart';
import '../engine/labels.dart';
import 'gain_slider.dart';

/// Chip 7 preset (đổi khi đang hát không click), vì sao chip đang chọn, preset gồm những gì, thanh
/// "Vang", lọc tiếng ồn micro.
class FxPanel extends StatelessWidget {
  const FxPanel({
    super.key,
    required this.genre,
    required this.preset,
    required this.source,
    required this.bpm,
    required this.mix,
    required this.gateOn,
    required this.monitorOn,
    required this.onGenre,
    required this.onMix,
    required this.onGate,
  });

  final Genre genre;
  final FxPreset preset;
  final GenreSource? source;
  final double? bpm;
  final double mix;
  final bool gateOn;
  final bool monitorOn;
  final ValueChanged<Genre> onGenre;
  final ValueChanged<double> onMix;
  final ValueChanged<bool> onGate;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final correction = genreCorrectionLabel(genreCorrection(genre));
    final src = source;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Hiệu ứng giọng', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (final g in Genre.values)
              ChoiceChip(
                label: Text(genreLabel(g)),
                labelStyle: theme.textTheme.titleMedium,
                labelPadding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 4,
                ),
                selected: g == genre,
                showCheckmark: false,
                onSelected: (_) => onGenre(g),
              ),
          ],
        ),
        const SizedBox(height: 8),
        Text(fxSummary(preset)),
        if (correction != null)
          Text(
            'Kèm chỉnh giọng: $correction',
            style: theme.textTheme.bodySmall,
          ),
        if (src != null)
          Text(
            genreSourceLabel(src, bpm: bpm),
            style: theme.textTheme.bodySmall,
          ),
        GainSlider(
          label: 'Vang',
          value: mix,
          max: 2,
          onChanged: genre == Genre.dry ? null : onMix,
        ),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('Lọc tiếng ồn micro'),
          subtitle: const Text('Giảm tiếng ồn nền lúc không hát'),
          value: gateOn,
          onChanged: onGate,
        ),
        if (!monitorOn)
          Text(
            'Bật "Nghe lại giọng" để nghe hiệu ứng khi hát.',
            style: theme.textTheme.bodySmall,
          ),
      ],
    );
  }
}
