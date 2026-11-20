import 'dart:async';
import 'dart:io';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/app_health.dart';
import '../data/latency_history.dart';
import '../data/test_log.dart';
import '../engine/audio_service.dart';
import '../engine/device_check.dart';
import '../engine/labels.dart';
import '../engine/latency_measure.dart';
import '../engine/take_alignment.dart';
import '../widgets/app_ui.dart';
import 'settings_page.dart' show appVersion;

typedef DeviceCheckStarter = DeviceCheckRun Function({
  required Duration audio,
  required String tempDir,
  void Function(double progress)? onProgress,
});

/// Cài đặt → Nâng cao → Kiểm tra máy (M9 B4, docs/m9-plan.md): bấm một nút, đợi, gửi kết quả —
/// cho đợt thử trên 13 máy mà người thử không cần máy tính.
class DeviceCheckPage extends StatefulWidget {
  const DeviceCheckPage({super.key, this.start = startDeviceCheck});

  final DeviceCheckStarter start;

  @override
  State<DeviceCheckPage> createState() => _DeviceCheckPageState();
}

class _DeviceCheckPageState extends State<DeviceCheckPage> {
  DeviceCheckRun? _run;
  double _progress = 0;
  DeviceCheckResult? _result;
  String? _error;

  @override
  void initState() {
    super.initState();
    TestLog.screen('device-check');
  }

  @override
  void dispose() {
    _run?.cancel();
    super.dispose();
  }

  Future<void> _begin(Duration audio) async {
    final run = widget.start(
      audio: audio,
      tempDir: Directory.systemTemp.path,
      onProgress: (p) {
        if (mounted) setState(() => _progress = p);
      },
    );
    setState(() {
      _run = run;
      _progress = 0;
      _result = null;
      _error = null;
    });
    try {
      final r = await run.result;
      if (!mounted) return;
      setState(() {
        _run = null;
        _result = r;
      });
      if (r != null) {
        TestLog.event('device-check', {
          'audioS': r.audio.inSeconds,
          'meanLoad': r.meanLoad,
          'p99Load': r.p99Load,
          'bad': r.badSamples,
        });
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _run = null;
        _error = 'Kiểm tra không chạy được: $e';
      });
    }
  }

  Future<String> _report(DeviceCheckResult r) async {
    final d = await DeviceLabel.load();
    final history = parseLatencyHistory(await LatencyHistory().load());
    final seen = <AudioRoute>{};
    final latency = [
      for (final e in history)
        if (seen.add(e.route))
          '${routeLabel(e.route)} ${e.measuredMs.toStringAsFixed(1).replaceAll('.', ',')} ms',
    ];
    int? xruns;
    try {
      xruns = AudioService.instance.engine?.metrics.xruns;
    } catch (_) {}
    await AppHealth.flush();
    final device = [
      d.brand,
      d.model,
      d.os,
    ].where((s) => s.isNotEmpty).join(' · ');
    return deviceCheckReport(
      r,
      at: DateTime.now(),
      device: device.isEmpty ? 'không rõ' : device,
      appVersion: appVersion,
      engineVersion: _engineVersion(),
      latency: latency,
      xruns: xruns,
      health: AppHealth.stats.summary,
    );
  }

  String? _engineVersion() {
    try {
      return ToneCore.version;
    } catch (_) {
      return null; // không nạp được engine (test, máy lỗi)
    }
  }

  Future<void> _share(DeviceCheckResult r) async {
    final text = await _report(r);
    final f = File('${Directory.systemTemp.path}/chuantone-kiem-tra-may.txt');
    await f.writeAsString(text);
    final ok = await ChuanToneMedia.share(f.path);
    if (ok == null && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Máy này chưa chia sẻ được. File: ${f.path}')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    final run = _run;
    final r = _result;
    return Scaffold(
      appBar: AppBar(title: const Text('Kiểm tra máy')),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
        children: [
          Text(
            'Đo xem máy có đủ khoẻ để chỉnh giọng mượt không. App chạy thử chuỗi xử lý giọng '
            'với giọng hát giả — không thu âm, không cần mạng.',
            style: theme.textTheme.bodyLarge,
          ),
          const SizedBox(height: 16),
          if (run != null) ...[
            Text(
              'Đang kiểm tra… ${(_progress * 100).round()}%',
              style: theme.textTheme.titleMedium,
            ),
            const SizedBox(height: 8),
            LinearProgressIndicator(value: _progress),
            Align(
              alignment: Alignment.centerRight,
              child: TextButton(
                onPressed: () {
                  run.cancel();
                  setState(() => _run = null);
                },
                child: const Text('Huỷ'),
              ),
            ),
          ] else ...[
            FilledButton.icon(
              onPressed: () => _begin(const Duration(seconds: 30)),
              icon: const Icon(Icons.speed),
              label: const Text('Kiểm tra nhanh'),
              style: FilledButton.styleFrom(
                minimumSize: const Size.fromHeight(56),
              ),
            ),
            const SizedBox(height: 4),
            Text(
              '30 giây âm thanh, vài giây là xong.',
              style: theme.textTheme.bodyMedium,
            ),
            const SizedBox(height: 12),
            OutlinedButton.icon(
              onPressed: () => _begin(const Duration(minutes: 30)),
              icon: const Icon(Icons.timer_outlined),
              label: const Text('Kiểm tra lâu'),
              style: OutlinedButton.styleFrom(
                minimumSize: const Size.fromHeight(56),
              ),
            ),
            const SizedBox(height: 4),
            Text(
              '30 phút âm thanh như hát liên tục, mất khoảng 1–2 phút — để máy yên.',
              style: theme.textTheme.bodyMedium,
            ),
          ],
          if (_error case final e?) ...[
            const SizedBox(height: 16),
            StatusCard(
              icon: Icons.error_outline,
              background: cs.errorContainer,
              foreground: cs.onErrorContainer,
              child: Text(e),
            ),
          ],
          if (r != null) ...[
            const SizedBox(height: 16),
            StatusCard(
              icon: r.level == DeviceCheckLevel.good
                  ? Icons.check_circle
                  : Icons.info,
              // Màu theo theme (sáng / tối) như các thẻ trạng thái khác.
              background: switch (r.level) {
                DeviceCheckLevel.good => cs.tertiaryContainer,
                DeviceCheckLevel.weak => cs.secondaryContainer,
                _ => cs.errorContainer,
              },
              foreground: switch (r.level) {
                DeviceCheckLevel.good => cs.onTertiaryContainer,
                DeviceCheckLevel.weak => cs.onSecondaryContainer,
                _ => cs.onErrorContainer,
              },
              child: Text(deviceCheckVerdict(r.level)),
            ),
            const SizedBox(height: 8),
            Card(
              child: Column(
                children: [
                  ListTile(
                    title: const Text('Chuỗi giọng dùng'),
                    subtitle: Text(deviceCheckLoadLine(r)),
                  ),
                  ListTile(
                    title: const Text('Nhanh hơn thời gian thực'),
                    subtitle: Text('${r.realtimeFactor.round()} lần'),
                  ),
                  ListTile(
                    title: const Text('Đã chạy thử'),
                    subtitle: Text(
                      '${formatDuration(r.audio)} âm thanh trong '
                      '${formatDuration(r.elapsed)}',
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 8),
            FilledButton.icon(
              onPressed: () => _share(r),
              icon: const Icon(Icons.ios_share),
              label: const Text('Gửi kết quả'),
              style: FilledButton.styleFrom(
                minimumSize: const Size.fromHeight(52),
              ),
            ),
          ],
        ],
      ),
    );
  }
}
