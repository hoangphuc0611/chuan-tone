import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

/// Chữ kỹ thuật của một lỗi, để ở nút "Chi tiết" (M8 B5: người dùng chỉ thấy câu thường kèm việc
/// cần làm).
String errorDetails(Object error) => switch (error) {
  ToneCoreException e => '${e.operation}: ${e.codeName}',
  PlatformException e => '${e.code}: ${e.message ?? ''}'.trim(),
  _ => error.toString(),
};

/// Hộp "Chi tiết lỗi": chữ kỹ thuật, sao chép được để gửi nhóm phát triển.
Future<void> showErrorDetails(BuildContext context, String details) =>
    showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Chi tiết lỗi'),
        content: SelectableText(details),
        actions: [
          TextButton(
            onPressed: () {
              Clipboard.setData(ClipboardData(text: details));
              Navigator.pop(ctx);
            },
            child: const Text('Sao chép'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Đóng'),
          ),
        ],
      ),
    );

/// Thanh thông báo lỗi; có chi tiết thì kèm nút "Chi tiết".
void showErrorSnack(BuildContext context, String text, {String? details}) {
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(
      SnackBar(
        content: Text(text),
        action: details == null
            ? null
            : SnackBarAction(
                label: 'Chi tiết',
                onPressed: () => showErrorDetails(context, details),
              ),
      ),
    );
}

/// Dòng lỗi (màu lỗi) + nút "Chi tiết" khi có chữ kỹ thuật.
class ErrorText extends StatelessWidget {
  const ErrorText(this.text, {super.key, this.details});

  final String text;
  final String? details;

  @override
  Widget build(BuildContext context) {
    final color = Theme.of(context).colorScheme.error;
    final d = details;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(text, style: TextStyle(color: color)),
        if (d != null)
          TextButton(
            style: TextButton.styleFrom(
              padding: EdgeInsets.zero,
              visualDensity: VisualDensity.compact,
            ),
            onPressed: () => showErrorDetails(context, d),
            child: const Text('Chi tiết'),
          ),
      ],
    );
  }
}
