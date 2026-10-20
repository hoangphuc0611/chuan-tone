import 'dart:io';

import 'package:device_info_plus/device_info_plus.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/audio_service.dart';
import '../engine/labels.dart';

/// Màn đo độ trễ round-trip (M1). Kết quả lưu trên máy và sao chép được dạng một dòng CSV để
/// gộp báo cáo bằng tools/latency/aggregate.py.
class LatencyPage extends StatefulWidget {
  const LatencyPage({super.key});

  /// Thứ tự cột CSV – giữ khớp với tools/latency/aggregate.py.
  static const csvHeader =
      'date,brand,model,os,route,api,sample_rate,burst_frames,buffer_frames,'
      'measured_ms,reported_ms,confidence,valid_bursts,spread_ms';

  @override
  State<LatencyPage> createState() => _LatencyPageState();
}

class _LatencyPageState extends State<LatencyPage> {
  static const _historyKey = 'latency_history_csv';

  final _service = AudioService.instance;
  bool _starting = true;
  bool _measuring = false;
  LatencyResult? _result;
  String? _error;
  List<String> _history = [];
  String _brand = '';
  String _model = '';
  String _os = '';

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    await _service.ensureStarted();
    await _loadDevice();
    final prefs = await SharedPreferences.getInstance();
    _history = prefs.getStringList(_historyKey) ?? [];
    if (mounted) setState(() => _starting = false);
  }

  Future<void> _loadDevice() async {
    final info = DeviceInfoPlugin();
    if (Platform.isAndroid) {
      final a = await info.androidInfo;
      _brand = a.manufacturer;
      _model = a.model;
      _os = 'Android ${a.version.release} (API ${a.version.sdkInt})';
    } else if (Platform.isIOS) {
      final i = await info.iosInfo;
      _brand = 'Apple';
      _model = i.utsname.machine;
      _os = 'iOS ${i.systemVersion}';
    }
  }

  Future<void> _measure() async {
    final e = _service.engine;
    if (e == null) return;
    setState(() {
      _measuring = true;
      _error = null;
    });
    final wasPlaying = e.playback.playing;
    if (wasPlaying) e.pause();
    try {
      final r = await e.measureLatency();
      _result = r;
      if (r.valid) {
        await _save(r, e.metrics);
      } else {
        _error =
            'Không nghe được tín hiệu đo. Hãy tăng âm lượng máy và thử lại '
            '(loa ngoài, nơi yên tĩnh) hoặc dùng dongle loopback.';
      }
    } catch (err) {
      _error = 'Đo thất bại: $err';
    }
    if (mounted) setState(() => _measuring = false);
  }

  String _csv(LatencyResult r, EngineMetrics m) {
    String q(String s) => '"${s.replaceAll('"', "'")}"';
    return [
      DateTime.now().toIso8601String(),
      q(_brand),
      q(_model),
      q(_os),
      r.route.name,
      m.audioApi.name,
      m.sampleRate,
      r.burstFrames,
      r.bufferFrames,
      r.latencyMs.toStringAsFixed(2),
      r.reportedRoundTripMs.toStringAsFixed(2),
      r.confidence.toStringAsFixed(2),
      r.validBursts,
      r.spreadMs.toStringAsFixed(3),
    ].join(',');
  }

  Future<void> _save(LatencyResult r, EngineMetrics m) async {
    _history = [_csv(r, m), ..._history].take(50).toList();
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(_historyKey, _history);
  }

  Future<void> _copy({bool all = false}) async {
    if (_history.isEmpty) return;
    final text = all
        ? [LatencyPage.csvHeader, ..._history].join('\n')
        : '${LatencyPage.csvHeader}\n${_history.first}';
    await Clipboard.setData(ClipboardData(text: text));
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Đã sao chép. Dán gửi cho nhóm phát triển.'),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(title: const Text('Đo độ trễ')),
      body: _starting
          ? const Center(child: CircularProgressIndicator())
          : _service.engine == null
          ? Center(child: Text(_service.lastError ?? 'Không mở được âm thanh'))
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                const Card(
                  child: Padding(
                    padding: EdgeInsets.all(12),
                    child: Text(
                      'Cách đo:\n'
                      '• Loa ngoài: rút tai nghe, để máy nơi yên tĩnh, tăng âm lượng khoảng 70%.\n'
                      '• Đường tai nghe: cắm dongle loopback (nối đầu ra tai nghe vào micro).\n'
                      'Máy sẽ phát tiếng "xè" khoảng 2 giây.',
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                FilledButton.icon(
                  onPressed: _measuring ? null : _measure,
                  icon: _measuring
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.speed),
                  label: Text(_measuring ? 'Đang đo…' : 'Đo'),
                  style: FilledButton.styleFrom(
                    minimumSize: const Size.fromHeight(64),
                  ),
                ),
                const SizedBox(height: 24),
                if (_error != null)
                  Text(
                    _error!,
                    style: TextStyle(color: theme.colorScheme.error),
                  ),
                if (_result case final r? when r.valid) ...[
                  Center(
                    child: Text(
                      '${r.latencyMs.toStringAsFixed(1)} ms',
                      style: theme.textTheme.displayMedium?.copyWith(
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                  Center(
                    child: Text('Độ trễ khứ hồi – ${routeLabel(r.route)}'),
                  ),
                  const SizedBox(height: 8),
                  Center(
                    child: Text(
                      'Độ tin cậy ${(r.confidence * 100).round()}% · '
                      '${r.validBursts}/${r.totalBursts} lần khớp · '
                      'HĐH báo ${r.reportedRoundTripMs.toStringAsFixed(1)} ms',
                      style: theme.textTheme.bodySmall,
                    ),
                  ),
                ],
                const Divider(height: 32),
                Row(
                  children: [
                    Expanded(
                      child: Text(
                        'Lịch sử (${_history.length})',
                        style: theme.textTheme.titleMedium,
                      ),
                    ),
                    TextButton.icon(
                      onPressed: _history.isEmpty ? null : () => _copy(),
                      icon: const Icon(Icons.copy),
                      label: const Text('Sao chép kết quả'),
                    ),
                  ],
                ),
                if (_history.length > 1)
                  Align(
                    alignment: Alignment.centerRight,
                    child: TextButton(
                      onPressed: () => _copy(all: true),
                      child: const Text('Sao chép tất cả'),
                    ),
                  ),
                for (final line in _history.take(10))
                  Text(_summary(line), style: theme.textTheme.bodySmall),
              ],
            ),
    );
  }

  /// Dòng tóm tắt dễ đọc từ một dòng CSV đã lưu.
  String _summary(String csv) {
    final parts = csv.split(',');
    if (parts.length < 10) return csv;
    final date = parts[0].length >= 16
        ? parts[0].substring(0, 16).replaceFirst('T', ' ')
        : parts[0];
    return '$date · ${parts[4]} · ${parts[9]} ms';
  }
}
