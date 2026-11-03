import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../data/lrc.dart';

/// Lời chạy chữ ở Màn hát (M8 B3): dòng đang hát (to, phần đã hát tô màu dần) + dòng sau (mờ).
/// Chiều cao cố định theo cỡ chữ (2 dòng chữ to + 1 dòng chữ nhỏ) để các nút bên dưới không nhảy
/// khi đổi câu. Chỉ dựng lại theo [position] (~30 lần/giây), không dựng lại cả màn.
class LyricsView extends StatelessWidget {
  const LyricsView({
    super.key,
    required this.lyrics,
    required this.offsetMs,
    required this.position,
  });

  final Lyrics lyrics;

  /// Lệch lời (ms, dương = lời hiện sớm hơn).
  final int offsetMs;
  final ValueListenable<Duration> position;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final scaler = MediaQuery.textScalerOf(context);
    final big = theme.textTheme.titleLarge!.copyWith(
      fontWeight: FontWeight.w700,
      height: 1.25,
    );
    final small = theme.textTheme.titleMedium!.copyWith(
      color: theme.colorScheme.onSurfaceVariant,
      height: 1.25,
    );
    final height =
        scaler.scale(big.fontSize!) * big.height! * 2 +
        scaler.scale(small.fontSize!) * small.height! +
        12;
    return SizedBox(
      height: height,
      child: ValueListenableBuilder<Duration>(
        valueListenable: position,
        builder: (context, pos, _) {
          final t = pos + Duration(milliseconds: offsetMs);
          final i = lyrics.indexAt(t);
          final lines = lyrics.lines;
          final current = i >= 0 ? lines[i].text : '';
          final next = i + 1 < lines.length ? lines[i + 1].text : '';
          final sung = lyrics.sungChars(i, t);
          return Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text.rich(
                TextSpan(
                  children: [
                    TextSpan(
                      text: current.substring(0, sung),
                      style: TextStyle(color: theme.colorScheme.primary),
                    ),
                    TextSpan(text: current.substring(sung)),
                  ],
                ),
                key: const ValueKey('lyrics-current'),
                style: big,
                textAlign: TextAlign.center,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
              const SizedBox(height: 4),
              Text(
                i < 0 && next.isNotEmpty ? '♪ $next' : next,
                key: const ValueKey('lyrics-next'),
                style: small,
                textAlign: TextAlign.center,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          );
        },
      ),
    );
  }
}
