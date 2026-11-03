import 'package:flutter/material.dart';

/// Hỏi một lần khi vừa cắm thiết bị USB (M8 B6): có phải soundcard không. Thiết bị USB cũng có thể
/// là tai nghe USB-C nên phải hỏi. true / false = đã chọn; null = đóng hộp (hỏi lại lần mở app sau).
Future<bool?> askSoundcardMode(BuildContext context) => showDialog<bool>(
  context: context,
  builder: (ctx) {
    final theme = Theme.of(ctx);
    return AlertDialog(
      icon: const Icon(Icons.settings_input_component),
      title: const Text('Bạn vừa cắm soundcard?'),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Nếu đây là soundcard (cắm micro riêng, có núm chỉnh vang), ChuẩnTone sẽ:',
            ),
            const SizedBox(height: 8),
            const Text(
              '• Tắt nghe lại giọng trong app, vì soundcard đã cho bạn nghe giọng trực tiếp.',
            ),
            const Text('• Giữ tiếng vang của soundcard trong bản thu.'),
            const SizedBox(height: 8),
            Text(
              'Đổi lại được ở Cài đặt → Âm thanh.',
              style: theme.textTheme.bodySmall,
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Không, đây là tai nghe'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Dùng chế độ soundcard'),
        ),
      ],
    );
  },
);

/// Lời giải thích lựa chọn hiệu ứng ở chế độ soundcard (Màn hát, Cài đặt).
const soundcardFxHelp = (
  soundcard:
      'Bản thu giữ nguyên tiếng vang của soundcard. Muốn dùng hiệu ứng của app thì tắt vang trên '
      'soundcard.',
  app:
      'Hiệu ứng của app có trong bản thu và khi nghe lại (lúc hát bạn nghe tiếng của soundcard). Nên '
      'tắt vang trên soundcard để không bị vang chồng vang.',
);
