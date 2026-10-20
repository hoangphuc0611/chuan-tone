import 'package:flutter/material.dart';

import 'engine_info.dart';
import 'pages/latency_page.dart';
import 'pages/test_room_page.dart';

class ChuanToneApp extends StatelessWidget {
  const ChuanToneApp({super.key, required this.engineInfo});

  final EngineInfo engineInfo;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ChuẩnTone',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFFE5484D)),
        useMaterial3: true,
      ),
      home: HomePage(engineInfo: engineInfo),
    );
  }
}

class HomePage extends StatelessWidget {
  const HomePage({super.key, required this.engineInfo});

  final EngineInfo engineInfo;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 32),
              Text(
                'ChuẩnTone',
                style: theme.textTheme.displaySmall?.copyWith(
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 8),
              Text('Bấm bài là hát', style: theme.textTheme.titleLarge),
              const Spacer(),
              _BigButton(
                icon: Icons.mic_external_on,
                label: 'Phòng thử',
                subtitle: 'Phát nhạc, nghe lại giọng',
                enabled: engineInfo.ok,
                onTap: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const TestRoomPage()),
                ),
              ),
              const SizedBox(height: 12),
              _BigButton(
                icon: Icons.speed,
                label: 'Đo độ trễ',
                subtitle: 'Kiểm tra máy có hát mượt không',
                enabled: engineInfo.ok,
                onTap: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const LatencyPage()),
                ),
              ),
              const SizedBox(height: 24),
              _EngineStatusCard(info: engineInfo),
              const SizedBox(height: 24),
            ],
          ),
        ),
      ),
    );
  }
}

class _BigButton extends StatelessWidget {
  const _BigButton({
    required this.icon,
    required this.label,
    required this.subtitle,
    required this.enabled,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final String subtitle;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return FilledButton.tonal(
      onPressed: enabled ? onTap : null,
      style: FilledButton.styleFrom(
        minimumSize: const Size.fromHeight(80),
        alignment: Alignment.centerLeft,
        padding: const EdgeInsets.symmetric(horizontal: 20),
      ),
      child: Row(
        children: [
          Icon(icon, size: 36),
          const SizedBox(width: 16),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(label, style: theme.textTheme.titleLarge),
              Text(subtitle, style: theme.textTheme.bodyMedium),
            ],
          ),
        ],
      ),
    );
  }
}

class _EngineStatusCard extends StatelessWidget {
  const _EngineStatusCard({required this.info});

  final EngineInfo info;

  @override
  Widget build(BuildContext context) {
    final ok = info.ok;
    final color = ok ? Colors.green : Theme.of(context).colorScheme.error;
    return Card(
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
