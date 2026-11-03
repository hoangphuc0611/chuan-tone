import 'dart:async';

import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../widgets/input_level_meter.dart';

/// Cài đặt → Micro (M8 B4): Tự động / Micro của máy / Micro tai nghe / Soundcard; micro đang dùng,
/// thanh mức micro để thử ("nói a lô"), cảnh báo khi micro bị vỡ tiếng (soundcard để gain quá to).
class MicPage extends StatefulWidget {
  const MicPage({super.key, this.service});

  final AudioService? service;

  @override
  State<MicPage> createState() => _MicPageState();
}

class _MicPageState extends State<MicPage> {
  late final AudioService _service = widget.service ?? AudioService.instance;
  bool _starting = true;
  EngineMetrics? _m;
  Timer? _timer;
  int _clipsSeen = -1;
  DateTime? _lastClip;

  @override
  void initState() {
    super.initState();
    unawaited(_init());
  }

  Future<void> _init() async {
    await _service.ensureStarted();
    if (!mounted) return;
    setState(() => _starting = false);
    _tick();
    _timer = Timer.periodic(const Duration(milliseconds: 100), (_) => _tick());
  }

  void _tick() {
    final m = _service.engine?.metrics;
    if (m == null || !mounted) return;
    if (_clipsSeen >= 0 && m.inputClips > _clipsSeen) {
      _lastClip = DateTime.now();
    }
    _clipsSeen = m.inputClips;
    setState(() => _m = m);
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> _pick(InputKind k) async {
    await _service.setInputPreference(k);
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final m = _m;
    final chosen = _service.inputPreference;
    final source = m?.inputSource ?? InputKind.auto;
    final clipping =
        _lastClip != null &&
        DateTime.now().difference(_lastClip!) < const Duration(seconds: 3);
    return Scaffold(
      appBar: AppBar(title: const Text('Micro')),
      body: _starting
          ? const Center(child: CircularProgressIndicator())
          : _service.engine == null
          ? Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text(
                  _service.lastError ?? 'Không mở được âm thanh.',
                  textAlign: TextAlign.center,
                ),
              ),
            )
          : ListView(
              padding: const EdgeInsets.symmetric(vertical: 8),
              children: [
                RadioGroup<InputKind>(
                  groupValue: chosen,
                  onChanged: (k) {
                    if (k != null) unawaited(_pick(k));
                  },
                  child: Column(
                    children: [
                      for (final k in const [
                        InputKind.auto,
                        InputKind.builtin,
                        InputKind.headset,
                        InputKind.usb,
                      ])
                        RadioListTile<InputKind>(
                          value: k,
                          title: Text(inputKindLabel(k)),
                          subtitle: Text(switch (k) {
                            InputKind.auto => 'Máy tự chọn: có tai nghe / soundcard thì dùng, không thì micro máy',
                            InputKind.builtin => 'Kể cả khi đang cắm tai nghe',
                            InputKind.headset => 'Micro trên dây tai nghe (cổng 3,5 mm hoặc adapter)',
                            _ => 'Cắm qua cáp OTG / cổng Lightning, USB-C',
                          }),
                        ),
                    ],
                  ),
                ),
                const Divider(height: 24),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Text(
                        'Đang dùng: ${source == InputKind.auto ? 'chưa rõ' : inputKindLabel(source)}',
                        key: const ValueKey('mic-source'),
                        style: theme.textTheme.titleMedium,
                      ),
                      if (chosen != InputKind.auto &&
                          source != InputKind.auto &&
                          source != chosen)
                        Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(
                            'Chưa thấy ${inputKindLabel(chosen).toLowerCase()}. Cắm vào là app tự '
                            'chuyển sang.',
                            style: theme.textTheme.bodyMedium,
                          ),
                        ),
                      const SizedBox(height: 16),
                      Text(
                        'Nói "a lô" để thử micro:',
                        style: theme.textTheme.bodyLarge,
                      ),
                      const SizedBox(height: 8),
                      InputLevelMeter(peakDb: m?.inputPeakDb ?? -120),
                      const SizedBox(height: 8),
                      if (clipping)
                        Text(
                          'Micro bị quá to (vỡ tiếng). Vặn nhỏ nút gain trên soundcard, hoặc để micro '
                          'xa miệng hơn.',
                          key: const ValueKey('mic-clipping'),
                          style: TextStyle(color: theme.colorScheme.error),
                        )
                      else
                        Text(
                          'Hát to nhất mà thanh vẫn chưa chạm vạch đỏ là vừa.',
                          style: theme.textTheme.bodySmall,
                        ),
                    ],
                  ),
                ),
              ],
            ),
    );
  }
}
