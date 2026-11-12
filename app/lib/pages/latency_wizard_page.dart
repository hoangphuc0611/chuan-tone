import 'dart:async';

import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/latency_history.dart';
import '../data/test_log.dart';
import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../engine/latency_measure.dart';
import '../widgets/latency_gauge.dart';

/// Đo độ trễ từng bước cho người dùng (M8 B4, Cài đặt → Âm thanh): rút tai nghe → để máy chỗ yên
/// tĩnh, đo qua loa → báo kết quả bằng lời ("Máy bạn hát mượt" / "Hơi trễ, nên dùng tai nghe có
/// dây"). Số đo được lưu để căn giọng khớp nhạc khi thu (M7). Chế độ soundcard (B6): đo qua
/// Loopback của soundcard — tiếng đo đi vòng bằng điện nên rất chính xác, lưu theo route USB. Bản kỹ
/// thuật: [LatencyPage].
class LatencyWizardPage extends StatefulWidget {
  const LatencyWizardPage({super.key, this.service, this.store, this.measure});

  final AudioService? service;
  final LatencyHistory? store;

  /// Đo (mặc định: engine của [service] + lưu lịch sử); test thay bằng hàm giả.
  final Future<LatencyResult> Function()? measure;

  @override
  State<LatencyWizardPage> createState() => _LatencyWizardPageState();
}

enum _Step { unplug, measure, result }

class _LatencyWizardPageState extends State<LatencyWizardPage> {
  late final AudioService _service = widget.service ?? AudioService.instance;
  late final LatencyHistory _store = widget.store ?? LatencyHistory();
  _Step _step = _Step.unplug;
  bool _starting = true;
  bool _measuring = false;
  LatencyResult? _result;
  AudioRoute _route = AudioRoute.unknown;
  Timer? _routeTimer;

  @override
  void initState() {
    super.initState();
    TestLog.screen('latency');
    unawaited(_init());
  }

  Future<void> _init() async {
    if (widget.measure == null) await _service.ensureStarted();
    if (!mounted) return;
    setState(() => _starting = false);
    _readRoute();
    _routeTimer = Timer.periodic(
      const Duration(milliseconds: 500),
      (_) => _readRoute(),
    );
  }

  /// Đang ở chế độ soundcard: đo qua Loopback thay vì qua loa.
  bool get _loop => _service.soundcardActive;

  void _readRoute() {
    final r = _service.engine?.metrics.route ?? AudioRoute.unknown;
    if (mounted && r != _route) setState(() => _route = r);
  }

  @override
  void dispose() {
    _routeTimer?.cancel();
    super.dispose();
  }

  Future<void> _measure() async {
    setState(() => _measuring = true);
    LatencyResult? r;
    try {
      final custom = widget.measure;
      if (custom != null) {
        r = await custom();
      } else {
        final e = _service.engine;
        if (e != null) {
          final (res, _) = await measureAndSaveLatency(
            e,
            store: _store,
            history: await _store.load(),
            device: await DeviceLabel.load(),
          );
          r = res;
        }
      }
    } catch (_) {
      r = null;
    }
    if (!mounted) return;
    setState(() {
      _measuring = false;
      _result = r;
      _step = _Step.result;
    });
  }

  @override
  Widget build(BuildContext context) {
    final noEngine =
        !_starting && widget.measure == null && _service.engine == null;
    return Scaffold(
      appBar: AppBar(title: const Text('Đo độ trễ')),
      body: _starting
          ? const Center(child: CircularProgressIndicator())
          : noEngine
          ? Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text(
                  _service.lastError ?? 'Không mở được âm thanh.',
                  textAlign: TextAlign.center,
                ),
              ),
            )
          : SafeArea(
              child: ListView(
                padding: const EdgeInsets.all(20),
                children: [
                  _StepDots(index: _step.index, count: _Step.values.length),
                  const SizedBox(height: 16),
                  ...switch (_step) {
                    _Step.unplug => _unplug(context),
                    _Step.measure => _measureStep(context),
                    _Step.result => _resultStep(context),
                  },
                ],
              ),
            ),
    );
  }

  List<Widget> _unplug(BuildContext context) {
    final theme = Theme.of(context);
    if (_loop) {
      return [
        Icon(
          Icons.settings_input_component,
          size: 72,
          color: theme.colorScheme.primary,
        ),
        const SizedBox(height: 16),
        Text(
          'Bước 1: Bật Loopback trên soundcard',
          style: theme.textTheme.headlineSmall,
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 8),
        Text(
          'Tiếng đo đi vòng trong soundcard (không qua không khí) nên rất chính xác. Bật nút '
          'Loopback trên soundcard (có máy ghi "Live" hoặc "Thu trực tiếp"), vặn nhỏ micro và tắt '
          'vang của soundcard trong lúc đo.',
          textAlign: TextAlign.center,
          style: theme.textTheme.bodyLarge,
        ),
        const SizedBox(height: 12),
        Text(
          'Đang dùng: ${routeLabel(AudioRoute.usb)}',
          key: const ValueKey('wizard-route'),
          textAlign: TextAlign.center,
          style: theme.textTheme.titleMedium?.copyWith(
            color: Theme.of(context).colorScheme.tertiary,
          ),
        ),
        const SizedBox(height: 24),
        FilledButton(
          onPressed: () => setState(() => _step = _Step.measure),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
          child: const Text('Tiếp'),
        ),
      ];
    }
    final speaker =
        _route == AudioRoute.speaker || _route == AudioRoute.unknown;
    return [
      Icon(
        speaker ? Icons.volume_up : Icons.headset_off,
        size: 72,
        color: theme.colorScheme.primary,
      ),
      const SizedBox(height: 16),
      Text(
        'Bước 1: Rút tai nghe',
        style: theme.textTheme.headlineSmall,
        textAlign: TextAlign.center,
      ),
      const SizedBox(height: 8),
      Text(
        'Máy đo bằng cách phát tiếng qua loa rồi nghe lại bằng micro. Hãy rút tai nghe và tắt '
        'Bluetooth.',
        textAlign: TextAlign.center,
        style: theme.textTheme.bodyLarge,
      ),
      const SizedBox(height: 12),
      Text(
        speaker
            ? 'Đang dùng: ${routeLabel(_route == AudioRoute.unknown ? AudioRoute.speaker : _route)}'
            : 'Đang dùng: ${routeLabel(_route)} — hãy rút ra để đo qua loa.',
        key: const ValueKey('wizard-route'),
        textAlign: TextAlign.center,
        style: theme.textTheme.titleMedium?.copyWith(
          color: speaker ? theme.colorScheme.tertiary : theme.colorScheme.error,
        ),
      ),
      const SizedBox(height: 24),
      FilledButton(
        onPressed: () => setState(() => _step = _Step.measure),
        style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        child: Text(speaker ? 'Tiếp' : 'Vẫn đo với thiết bị này'),
      ),
    ];
  }

  List<Widget> _measureStep(BuildContext context) {
    final theme = Theme.of(context);
    return [
      Icon(Icons.speed, size: 72, color: theme.colorScheme.primary),
      const SizedBox(height: 16),
      Text(
        _loop ? 'Bước 2: Đo qua soundcard' : 'Bước 2: Đo qua loa',
        style: theme.textTheme.headlineSmall,
        textAlign: TextAlign.center,
      ),
      const SizedBox(height: 8),
      Text(
        _loop
            ? 'Bỏ tai nghe ra hoặc vặn nhỏ: tiếng đo xè xè khoảng 2 giây khá to. Đừng nói vào micro '
                  'trong lúc đo.'
            : 'Để máy chỗ yên tĩnh, tăng âm lượng khoảng 70%. Máy sẽ kêu xè xè khoảng 2 giây — đừng '
                  'nói trong lúc đo.',
        textAlign: TextAlign.center,
        style: theme.textTheme.bodyLarge,
      ),
      const SizedBox(height: 24),
      FilledButton.icon(
        onPressed: _measuring ? null : _measure,
        icon: _measuring
            ? const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.play_arrow),
        label: Text(_measuring ? 'Đang đo…' : 'Bắt đầu đo'),
        style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
      ),
    ];
  }

  List<Widget> _resultStep(BuildContext context) {
    final theme = Theme.of(context);
    final r = _result;
    if (r == null || !r.valid) {
      return [
        Icon(Icons.hearing_disabled, size: 72, color: theme.colorScheme.error),
        const SizedBox(height: 16),
        Text(
          'Chưa đo được',
          style: theme.textTheme.headlineSmall,
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 8),
        Text(
          _loop
              ? 'Máy chưa nghe được tiếng đo. Kiểm tra nút Loopback trên soundcard đã bật và âm '
                    'lượng điện thoại không vặn nhỏ hết, rồi đo lại.'
              : 'Máy chưa nghe được tiếng đo. Hãy tăng âm lượng, để máy chỗ yên tĩnh rồi đo lại.',
          textAlign: TextAlign.center,
          style: theme.textTheme.bodyLarge,
        ),
        const SizedBox(height: 24),
        FilledButton(
          onPressed: () => setState(() => _step = _Step.measure),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
          child: const Text('Đo lại'),
        ),
      ];
    }
    // Soundcard đã cho nghe giọng trực tiếp: độ trễ chỉ dùng để căn bản thu, không cần đánh giá.
    final v = _loop
        ? (
            level: LatencyLevel.smooth,
            title: 'Đã đo qua soundcard',
            advice:
                'Bản thu sẽ được căn giọng khớp nhạc theo số đo này. Nhớ tắt Loopback trước khi '
                'thu, nếu không giọng sẽ lẫn nhạc.',
          )
        : latencyVerdict(r.latencyMs);
    final color = switch (v.level) {
      LatencyLevel.smooth => Theme.of(context).colorScheme.tertiary,
      LatencyLevel.slight => Theme.of(context).colorScheme.secondary,
      LatencyLevel.high => theme.colorScheme.error,
    };
    return [
      if (_loop)
        Icon(Icons.check_circle, size: 72, color: color)
      else
        Center(child: LatencyGauge(ms: r.latencyMs)),
      const SizedBox(height: 16),
      Text(
        v.title,
        key: const ValueKey('wizard-verdict'),
        style: theme.textTheme.headlineSmall?.copyWith(
          color: color,
          fontWeight: FontWeight.w700,
        ),
        textAlign: TextAlign.center,
      ),
      const SizedBox(height: 8),
      Text(
        v.advice,
        textAlign: TextAlign.center,
        style: theme.textTheme.bodyLarge,
      ),
      const SizedBox(height: 8),
      Text(
        _loop
            ? 'Độ trễ đo được: ${r.latencyMs.round()} ms · đã lưu để căn giọng khi thu.'
            : 'Đã lưu số đo để căn giọng khi thu.',
        textAlign: TextAlign.center,
        style: theme.textTheme.bodySmall,
      ),
      const SizedBox(height: 24),
      FilledButton(
        onPressed: () => Navigator.of(context).pop(),
        style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        child: const Text('Xong'),
      ),
      TextButton(
        onPressed: () => setState(() => _step = _Step.measure),
        child: const Text('Đo lại'),
      ),
    ];
  }
}

/// Chấm tiến độ các bước.
class _StepDots extends StatelessWidget {
  const _StepDots({required this.index, required this.count});

  final int index;
  final int count;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        for (var i = 0; i < count; i++)
          Container(
            width: i == index ? 24 : 10,
            height: 10,
            margin: const EdgeInsets.symmetric(horizontal: 4),
            decoration: BoxDecoration(
              color: i <= index
                  ? theme.colorScheme.primary
                  : theme.colorScheme.surfaceContainerHighest,
              borderRadius: BorderRadius.circular(5),
            ),
          ),
      ],
    );
  }
}
