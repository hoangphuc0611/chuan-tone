import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/latency_history.dart';
import '../data/test_log.dart';
import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../engine/latency_measure.dart';
import '../widgets/sync_check_card.dart';

/// Màn đo độ trễ round-trip kỹ thuật (M1; M8: Cài đặt → Nâng cao — người dùng đo ở
/// [LatencyWizardPage]). Kết quả lưu trên máy và sao chép được dạng một dòng CSV để gộp báo cáo bằng
/// tools/latency/aggregate.py.
class LatencyPage extends StatefulWidget {
  const LatencyPage({super.key});

  /// Thứ tự cột CSV – giữ khớp với tools/latency/aggregate.py.
  static const csvHeader = latencyCsvHeader;

  @override
  State<LatencyPage> createState() => _LatencyPageState();
}

class _LatencyPageState extends State<LatencyPage> {
  final _service = AudioService.instance;
  final _store = LatencyHistory();
  bool _starting = true;
  bool _measuring = false;
  LatencyResult? _result;
  String? _error;
  List<String> _history = [];
  DeviceLabel _device = const DeviceLabel();

  @override
  void initState() {
    super.initState();
    TestLog.screen('latency_tech');
    _init();
  }

  Future<void> _init() async {
    // Mỗi bước có thể lỗi riêng (thiết bị âm thanh, plugin) → không để màn quay mãi.
    try {
      await _service.ensureStarted();
    } catch (err) {
      _error = 'Không mở được âm thanh: $err';
    }
    _device = await DeviceLabel.load();
    _history = await _store.load();
    if (mounted) setState(() => _starting = false);
  }

  Future<void> _measure() async {
    final e = _service.engine;
    if (e == null) return;
    setState(() {
      _measuring = true;
      _error = null;
    });
    try {
      final (r, history) = await measureAndSaveLatency(
        e,
        store: _store,
        history: _history,
        device: _device,
      );
      _result = r;
      _history = history;
      if (!r.valid) {
        _error =
            'Không nghe được tín hiệu đo. Hãy tăng âm lượng máy và thử lại '
            '(loa ngoài, nơi yên tĩnh) hoặc dùng dongle loopback.';
      }
    } catch (err) {
      _error = 'Đo thất bại: $err';
    }
    if (mounted) setState(() => _measuring = false);
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
                const SizedBox(height: 16),
                SyncCheckCard(history: _history, model: _device.model),
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
