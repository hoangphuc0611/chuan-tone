import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../widgets/pitch_meter.dart';

/// Màn thử M1–M2: phát beat, nghe lại giọng, tự chỉnh giọng, đồng hồ cao độ, số đo engine.
class TestRoomPage extends StatefulWidget {
  const TestRoomPage({super.key});

  @override
  State<TestRoomPage> createState() => _TestRoomPageState();
}

class _TestRoomPageState extends State<TestRoomPage> {
  final _service = AudioService.instance;
  Timer? _ticker;
  Timer? _pitchTicker;
  PitchState? _pitch;
  StreamSubscription<ToneCoreEvent>? _sub;
  bool _starting = true;
  bool _loading = false;
  String? _beatName;
  String? _message;
  PlaybackState? _playback;
  EngineMetrics? _metrics;
  double _beatGain = 0.8;
  double _monitorGain = 1.0;
  bool _monitorOn = false;
  double? _dragPosition;

  @override
  void initState() {
    super.initState();
    _start();
  }

  Future<void> _start() async {
    final ok = await _service.ensureStarted();
    if (!mounted) return;
    setState(() => _starting = false);
    if (!ok) return;
    final e = _service.engine!;
    e.beatGain = _beatGain;
    _monitorOn = e.monitorEnabled;
    _monitorGain = e.monitorGain;
    _sub = _service.events.listen(_onEvent);
    _ticker = Timer.periodic(
      const Duration(milliseconds: 250),
      (_) => _refresh(),
    );
    // Đồng hồ cao độ cần mượt hơn: ~30 lần/giây.
    _pitchTicker = Timer.periodic(const Duration(milliseconds: 33), (_) {
      final eng = _service.engine;
      if (eng == null || !mounted) return;
      setState(() => _pitch = eng.pitchState);
    });
  }

  void _refresh() {
    final e = _service.engine;
    if (e == null || !mounted) return;
    setState(() {
      _playback = e.playback;
      _metrics = e.metrics;
    });
  }

  void _onEvent(ToneCoreEvent ev) {
    if (!mounted) return;
    switch (ev.type) {
      case ToneCoreEventType.monitorBlocked:
        _show(
          'Tai nghe Bluetooth bị trễ nhiều nên đã tắt nghe lại giọng. Hãy dùng tai nghe có dây.',
        );
      case ToneCoreEventType.interrupted:
        if (ev.code == 1) _show('Âm thanh bị gián đoạn (cuộc gọi…)');
      case ToneCoreEventType.streamRestarted:
        if (!ev.ok) {
          _show(
            'Thiết bị âm thanh lỗi: ${ToneCoreException.resultName(ev.code)}',
          );
        }
      default:
        break;
    }
  }

  void _show(String text) => setState(() => _message = text);

  Future<void> _pickBeat() async {
    final e = _service.engine;
    if (e == null) return;
    // Lọc theo đuôi file: trên iOS mở app Tệp (không cần quyền thư viện nhạc).
    final files = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: const ['mp3', 'm4a', 'aac', 'wav', 'flac'],
    );
    if (files.isEmpty) return; // người dùng huỷ
    final path = files.single.path;
    if (path == null) return;
    setState(() {
      _loading = true;
      _message = null;
    });
    try {
      await e.loadBeatAndWait(path);
      _beatName = files.single.name;
    } on ToneCoreException catch (err) {
      _show('Không mở được file (${err.codeName}). Hãy thử MP3, M4A hoặc WAV.');
    } on TimeoutException {
      _show('Nạp bài quá lâu.');
    }
    if (mounted) setState(() => _loading = false);
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _pitchTicker?.cancel();
    _sub?.cancel();
    _service.engine?.pause();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Phòng thử')),
      body: _starting
          ? const Center(child: CircularProgressIndicator())
          : _service.engine == null
          ? _ErrorView(message: _service.lastError ?? 'Không mở được âm thanh')
          : _buildBody(context),
    );
  }

  Widget _buildBody(BuildContext context) {
    final e = _service.engine!;
    final pb = _playback;
    final m = _metrics;
    final theme = Theme.of(context);
    final duration = pb?.duration ?? Duration.zero;
    final position = pb?.position ?? Duration.zero;
    final maxMs = duration.inMilliseconds.toDouble();
    final speakerWarning = _monitorOn && m?.route == AudioRoute.speaker;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        if (_message != null)
          Card(
            color: theme.colorScheme.errorContainer,
            child: ListTile(
              leading: const Icon(Icons.info_outline),
              title: Text(_message!),
              trailing: IconButton(
                icon: const Icon(Icons.close),
                onPressed: () => setState(() => _message = null),
              ),
            ),
          ),
        // ---- Beat ----
        Text('Nhạc beat', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        FilledButton.icon(
          onPressed: _loading ? null : _pickBeat,
          icon: _loading
              ? const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.library_music),
          label: Text(_beatName ?? 'Chọn file nhạc'),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        ),
        if (pb case final p? when p.hasBeat) ...[
          Slider(
            value: (_dragPosition ?? position.inMilliseconds.toDouble()).clamp(
              0,
              maxMs,
            ),
            max: maxMs > 0 ? maxMs : 1,
            onChanged: (v) => setState(() => _dragPosition = v),
            onChangeEnd: (v) {
              e.seek(Duration(milliseconds: v.round()));
              _dragPosition = null;
            },
          ),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(formatDuration(position)),
              Text(formatDuration(duration)),
            ],
          ),
          const SizedBox(height: 8),
          Center(
            child: IconButton.filled(
              iconSize: 48,
              onPressed: () => p.playing ? e.pause() : e.play(),
              icon: Icon(p.playing ? Icons.pause : Icons.play_arrow),
            ),
          ),
        ],
        _GainSlider(
          label: 'Âm lượng nhạc',
          value: _beatGain,
          onChanged: (v) {
            setState(() => _beatGain = v);
            e.beatGain = v;
          },
        ),
        const Divider(height: 32),
        // ---- Nghe lại giọng ----
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: Text('Nghe lại giọng', style: theme.textTheme.titleLarge),
          subtitle: const Text('Nghe giọng mình qua tai nghe khi hát'),
          value: _monitorOn,
          onChanged: (v) {
            setState(() => _monitorOn = v);
            e.monitorEnabled = v;
          },
        ),
        if (speakerWarning)
          Text(
            'Đang dùng loa ngoài: dễ bị hú. Hãy cắm tai nghe có dây.',
            style: TextStyle(color: theme.colorScheme.error),
          ),
        _GainSlider(
          label: 'Âm lượng giọng',
          value: _monitorGain,
          max: 2,
          onChanged: (v) {
            setState(() => _monitorGain = v);
            e.monitorGain = v;
          },
        ),
        const Divider(height: 32),
        // ---- Tự chỉnh giọng ----
        Text('Tự chỉnh giọng', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        _KeyPicker(
          value: _service.key,
          onChanged: (k) => setState(() => _service.setKey(k)),
        ),
        const SizedBox(height: 12),
        SegmentedButton<int>(
          segments: [
            const ButtonSegment(value: -1, label: Text('Tắt')),
            for (final p in CorrectionPreset.values)
              ButtonSegment(
                value: p.index,
                label: Text(correctionPresetLabel(p)),
              ),
          ],
          selected: {_service.correction?.index ?? -1},
          showSelectedIcon: false,
          onSelectionChanged: (sel) => setState(() {
            final v = sel.single;
            _service.setCorrection(v < 0 ? null : CorrectionPreset.values[v]);
          }),
        ),
        const SizedBox(height: 4),
        Text(
          _service.correction == null
              ? 'Đồng hồ cao độ vẫn hoạt động khi tắt.'
              : _monitorOn
              ? 'Giọng nghe lại trễ thêm 10 ms khi bật tự chỉnh giọng.'
              : 'Bật "Nghe lại giọng" để nghe giọng đã chỉnh.',
          style: theme.textTheme.bodySmall,
        ),
        const SizedBox(height: 8),
        PitchMeter(state: _pitch),
        const Divider(height: 32),
        // ---- Số đo ----
        if (m != null) _MetricsCard(metrics: m),
      ],
    );
  }
}

/// Chọn tone: chủ âm (12 nốt) + thang âm.
class _KeyPicker extends StatelessWidget {
  const _KeyPicker({required this.value, required this.onChanged});

  final MusicKey value;
  final ValueChanged<MusicKey> onChanged;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Tone: ${keyLabel(value)}',
          style: theme.textTheme.headlineSmall?.copyWith(
            fontWeight: FontWeight.w700,
          ),
        ),
        Wrap(
          spacing: 16,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            DropdownButton<int>(
              value: value.root,
              items: [
                for (var r = 0; r < 12; r++)
                  DropdownMenuItem(
                    value: r,
                    child: Text('${noteNameVi(r)} (${noteLetter(r)})'),
                  ),
              ],
              onChanged: (r) {
                if (r != null) onChanged(MusicKey(r, value.scale));
              },
            ),
            DropdownButton<MusicScale>(
              value: value.scale,
              items: [
                for (final s in MusicScale.values)
                  DropdownMenuItem(value: s, child: Text(scaleLabel(s))),
              ],
              onChanged: (s) {
                if (s != null) onChanged(MusicKey(value.root, s));
              },
            ),
          ],
        ),
      ],
    );
  }
}

class _GainSlider extends StatelessWidget {
  const _GainSlider({
    required this.label,
    required this.value,
    required this.onChanged,
    this.max = 1,
  });

  final String label;
  final double value;
  final double max;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(width: 120, child: Text(label)),
        Expanded(
          child: Slider(value: value, max: max, onChanged: onChanged),
        ),
        SizedBox(width: 44, child: Text('${(value * 100).round()}%')),
      ],
    );
  }
}

class _MetricsCard extends StatelessWidget {
  const _MetricsCard({required this.metrics});

  final EngineMetrics metrics;

  @override
  Widget build(BuildContext context) {
    final m = metrics;
    final rows = <(String, String)>[
      ('Đường âm thanh', routeLabel(m.route)),
      ('API', apiLabel(m.audioApi)),
      ('Tần số', '${m.sampleRate} Hz'),
      (
        'Buffer',
        '${m.bufferFrames} mẫu (${m.bufferMs.toStringAsFixed(1)} ms), burst ${m.burstFrames}',
      ),
      ('HĐH báo trễ', _reported(m)),
      (
        'CPU',
        '${(m.cpuLoad * 100).toStringAsFixed(1)} % (đỉnh ${(m.cpuLoadPeak * 100).toStringAsFixed(1)} %)',
      ),
      ('Trễ xử lý', '${m.algorithmLatencyMs.toStringAsFixed(1)} ms'),
      ('Xrun (rè)', '${m.xruns}'),
    ];
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Thông số kỹ thuật',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 8),
            for (final (k, v) in rows)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 2),
                child: Row(
                  children: [
                    SizedBox(width: 130, child: Text(k)),
                    Expanded(
                      child: Text(
                        v,
                        style: const TextStyle(fontWeight: FontWeight.w600),
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// Độ trễ do hệ điều hành tự báo; một số máy (và máy ảo) trả số vô lý → hiện "không rõ".
String _reported(EngineMetrics m) {
  final ms = m.reportedInputLatencyMs + m.reportedOutputLatencyMs;
  if (ms <= 0 || ms > 1000) return 'không rõ';
  return '${ms.toStringAsFixed(1)} ms';
}

class _ErrorView extends StatelessWidget {
  const _ErrorView({required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.mic_off,
              size: 64,
              color: Theme.of(context).colorScheme.error,
            ),
            const SizedBox(height: 16),
            Text(message, textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}
