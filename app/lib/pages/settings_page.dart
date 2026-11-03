import 'package:flutter/material.dart';

import '../engine_info.dart';
import 'latency_page.dart';
import 'test_room_page.dart';

/// Cài đặt (M8). B3: khung đầu tiên — Đo độ trễ, Phòng thử (Nâng cao), thông tin engine (Giới
/// thiệu) chuyển từ Trang chủ vào đây. B4 thêm đủ các nhóm ở docs/m8-plan.md mục 1.
class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.engineInfo});

  final EngineInfo engineInfo;

  @override
  Widget build(BuildContext context) {
    final ok = engineInfo.ok;
    void open(Widget page) =>
        Navigator.of(context)
            .push(MaterialPageRoute<void>(builder: (_) => page));
    return Scaffold(
      appBar: AppBar(title: const Text('Cài đặt')),
      body: ListView(
        children: [
          const _Header('Âm thanh'),
          ListTile(
            leading: const Icon(Icons.speed),
            title: const Text('Đo độ trễ'),
            subtitle: const Text('Kiểm tra máy có hát mượt không'),
            enabled: ok,
            onTap: () => open(const LatencyPage()),
          ),
          const _Header('Nâng cao'),
          ListTile(
            leading: const Icon(Icons.science_outlined),
            title: const Text('Phòng thử'),
            subtitle: const Text(
              'Mọi nút và thông số kỹ thuật, máy chủ tách giọng',
            ),
            enabled: ok,
            onTap: () => open(const TestRoomPage()),
          ),
          const _Header('Giới thiệu'),
          EngineStatusCard(info: engineInfo),
        ],
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 20, 16, 4),
      child: Text(
        text,
        style: theme.textTheme.titleSmall?.copyWith(
          color: theme.colorScheme.primary,
        ),
      ),
    );
  }
}

/// Phiên bản engine / lỗi khởi động engine.
class EngineStatusCard extends StatelessWidget {
  const EngineStatusCard({super.key, required this.info});

  final EngineInfo info;

  @override
  Widget build(BuildContext context) {
    final ok = info.ok;
    final color = ok ? Colors.green : Theme.of(context).colorScheme.error;
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            Icon(ok ? Icons.check_circle : Icons.error, color: color, size: 32),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    ok
                        ? 'ToneCore v${info.version} – engine OK'
                        : 'Không khởi động được engine',
                    key: const Key('engine-status'),
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  if (ok)
                    Text(
                      'API ${info.apiVersion} · đã xử lý thử ${info.framesProcessed} mẫu',
                    )
                  else if (info.error != null)
                    Text(info.error!),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
