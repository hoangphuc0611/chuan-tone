import 'package:flutter/material.dart';

import 'engine_info.dart';

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
              _EngineStatusCard(info: engineInfo),
              const SizedBox(height: 24),
            ],
          ),
        ),
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
