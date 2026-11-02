import 'dart:async';
import 'dart:io';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/take_store.dart';
import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../engine/takes.dart';
import '../widgets/fx_panel.dart';
import '../widgets/gain_slider.dart';

/// Màn Nghe lại & chỉnh (CLAUDE.md mục 5 màn 4, M7): phát bản thu với giọng khô đi qua chuỗi giọng
/// real-time (đổi preset / mức chỉnh giọng nghe ngay), to nhỏ giọng / nhạc, giọng sớm / muộn, cắt
/// đầu đuôi; xuất M4A (−14 LUFS) và chia sẻ.
class ReviewPage extends StatefulWidget {
  const ReviewPage({super.key, required this.take, this.store});

  final Take take;
  final TakeStore? store;

  @override
  State<ReviewPage> createState() => _ReviewPageState();
}

class _ReviewPageState extends State<ReviewPage> {
  static const _nudgeStepMs = 5.0;

  final _service = AudioService.instance;
  late final TakeStore _store = widget.store ?? TakeStore();
  late TakeInfo _info = widget.take.info;
  Timer? _ticker;
  bool _loading = true;
  String? _error;
  PlaybackState? _playback;
  double? _drag;
  RangeValues? _trimDrag;

  TakeRenderer? _renderer;
  RenderStatus? _render;
  String? _exportPath;
  String? _exportError;

  Take get _take => widget.take;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    if (!await _service.ensureStarted()) {
      if (mounted) {
        setState(() {
          _loading = false;
          _error = _service.lastError ?? 'Không mở được âm thanh';
        });
      }
      return;
    }
    final e = _service.engine!;
    e.pause();
    _service.applyVoiceSettings(
      genre: _info.genre,
      correction: _info.correction,
      retuneMs: _info.retuneMs,
      fxMix: _info.fxMix,
      gateOn: _info.gateOn,
    );
    await _load();
    _ticker = Timer.periodic(const Duration(milliseconds: 200), (_) {
      final eng = _service.engine;
      if (eng != null && mounted) setState(() => _playback = eng.playback);
    });
  }

  /// Nạp (lại) bản thu với độ lệch hiện tại; giữ vị trí / đang phát.
  Future<void> _load({Duration? at, bool play = false}) async {
    final e = _service.engine;
    if (e == null) return;
    setState(() => _loading = true);
    try {
      await e.loadTakeAndWait(
        _take.beatPath,
        _take.dryPath,
        voiceOffsetFrames: _info.voiceOffsetFrames,
      );
      // Beat đã ở tone lúc hát → key đang nghe; chuyển tone theo thời gian bản thu.
      e.key = _info.key;
      if (_info.timeline.length > 1) e.setKeyTimeline(_info.timeline);
      e.tuningCents = _info.tuningCents;
      e.tempo = _info.tempoBpm;
      e.monitorGain = _info.voiceGain;
      e.beatGain = _info.beatGain;
      if (at != null) e.seek(at);
      if (play) e.play();
      _error = null;
    } on ToneCoreException catch (err) {
      _error = 'Không mở được bản thu (${err.codeName}).';
    } on TimeoutException {
      _error = 'Mở bản thu quá lâu.';
    }
    if (mounted) {
      setState(() {
        _loading = false;
        _playback = e.playback;
      });
    }
  }

  void _update(TakeInfo info) {
    setState(() => _info = info);
    unawaited(_store.save(_take.dir, info));
  }

  /// Chuỗi giọng đổi trên AudioService (chip thể loại, nút chỉnh giọng…) → ghi vào bản thu.
  void _syncVoice() => _update(
    _info.copyWith(
      genre: _service.genre,
      correction: () => _service.correction,
      retuneMs: () => _service.correctionRetuneMs,
      fxMix: _service.fxMix,
      gateOn: _service.gateOn,
    ),
  );

  Future<void> _nudge(double deltaMs) async {
    final pb = _service.engine?.playback;
    _update(
      _info.copyWith(nudgeMs: (_info.nudgeMs + deltaMs).clamp(-100, 100)),
    );
    await _load(at: pb?.position, play: pb?.playing ?? false);
  }

  Future<void> _export() async {
    final r = _renderer = TakeRenderer.create();
    final t = DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    final out =
        '${_take.dir.path}/xuat-${two(t.hour)}${two(t.minute)}${two(t.second)}.m4a';
    setState(() {
      _exportError = null;
      _exportPath = null;
      _render = null;
    });
    try {
      final s = await r.render(
        _take.beatPath,
        _take.dryPath,
        out,
        renderSettingsFor(_info, preset: _service.presetOf(_info.genre)),
        onProgress: (s) {
          if (mounted) setState(() => _render = s);
        },
      );
      if (!mounted) return;
      setState(() {
        _render = s;
        _exportPath = out;
      });
    } on ToneCoreException catch (err) {
      if (mounted && err.code != TcResult.cancelled) {
        setState(() => _exportError = 'Xuất lỗi (${err.codeName}).');
      }
    } finally {
      r.dispose();
      _renderer = null;
      if (mounted) setState(() {});
    }
  }

  Future<void> _share() async {
    final path = _exportPath;
    if (path == null) return;
    final ok = await ChuanToneMedia.share(path);
    if (ok == null && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Máy này chưa chia sẻ được. File: $path')),
      );
    }
  }

  Future<void> _delete() async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Xoá bản thu?'),
        content: const Text('Xoá cả các file đã xuất của bản thu này.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Huỷ'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Xoá'),
          ),
        ],
      ),
    );
    if (yes != true || !mounted) return;
    _service.engine?.pause();
    await _store.delete(_take);
    if (mounted) Navigator.of(context).pop();
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _renderer?.cancel();
    _service.engine?.pause();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Nghe lại & chỉnh'),
        actions: [
          IconButton(
            tooltip: 'Xoá bản thu',
            onPressed: _delete,
            icon: const Icon(Icons.delete_outline),
          ),
        ],
      ),
      body: _buildBody(context),
    );
  }

  Widget _buildBody(BuildContext context) {
    final theme = Theme.of(context);
    final e = _service.engine;
    final pb = _playback;
    final total = _info.duration;
    final totalMs = total.inMilliseconds.toDouble();
    final pos = pb?.position ?? Duration.zero;
    final rate = _info.sampleRate;
    double ms(int frames) => frames * 1000 / rate;
    final trimEnd = _info.trimEndFrame == 0 ? _info.frames : _info.trimEndFrame;
    final trim =
        _trimDrag ?? RangeValues(ms(_info.trimStartFrame), ms(trimEnd));
    final rendering = _renderer != null;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text(
          _info.songName,
          style: theme.textTheme.titleLarge,
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
        ),
        Text(
          '${formatTakeDate(_info.createdAt)} · ${formatDuration(total)} · '
          '${routeLabel(_info.route)}',
          style: theme.textTheme.bodySmall,
        ),
        if (_info.droppedFrames > 0)
          Text(
            'Bản thu có chỗ bị hụt (máy bận). Có thể nghe thấy tiếng ngắt.',
            style: TextStyle(color: theme.colorScheme.error),
          ),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(top: 8),
            child: Text(
              _error!,
              style: TextStyle(color: theme.colorScheme.error),
            ),
          ),
        const SizedBox(height: 8),
        // ---- Phát ----
        Slider(
          value: (_drag ?? pos.inMilliseconds.toDouble()).clamp(0, totalMs),
          max: totalMs > 0 ? totalMs : 1,
          onChanged: _loading ? null : (v) => setState(() => _drag = v),
          onChangeEnd: (v) {
            e?.seek(Duration(milliseconds: v.round()));
            _drag = null;
          },
        ),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [Text(formatDuration(pos)), Text(formatDuration(total))],
        ),
        Center(
          child: _loading
              ? const Padding(
                  padding: EdgeInsets.all(12),
                  child: CircularProgressIndicator(),
                )
              : IconButton.filled(
                  iconSize: 48,
                  onPressed: e == null || pb == null || !pb.hasTake
                      ? null
                      : () => pb.playing ? e.pause() : e.play(),
                  icon: Icon(
                    pb?.playing ?? false ? Icons.pause : Icons.play_arrow,
                  ),
                ),
        ),
        const Divider(height: 32),
        // ---- Trộn ----
        Text('Giọng và nhạc', style: theme.textTheme.titleLarge),
        GainSlider(
          label: 'Âm lượng giọng',
          value: _info.voiceGain,
          max: 2,
          onChanged: (v) {
            e?.monitorGain = v;
            _update(_info.copyWith(voiceGain: v));
          },
        ),
        GainSlider(
          label: 'Âm lượng nhạc',
          value: _info.beatGain,
          max: 2,
          onChanged: (v) {
            e?.beatGain = v;
            _update(_info.copyWith(beatGain: v));
          },
        ),
        const SizedBox(height: 8),
        Text('Giọng sớm / muộn', style: theme.textTheme.titleMedium),
        Row(
          children: [
            IconButton.outlined(
              tooltip: 'Giọng muộn hơn',
              onPressed: _loading ? null : () => _nudge(-_nudgeStepMs),
              icon: const Icon(Icons.remove),
            ),
            Expanded(
              child: Text(
                nudgeLabel(_info.nudgeMs),
                textAlign: TextAlign.center,
                style: theme.textTheme.titleMedium,
              ),
            ),
            IconButton.outlined(
              tooltip: 'Giọng sớm hơn',
              onPressed: _loading ? null : () => _nudge(_nudgeStepMs),
              icon: const Icon(Icons.add),
            ),
          ],
        ),
        Text(
          'Nghe giọng chậm hơn nhạc thì bấm +. Đã bù độ trễ '
          '${(_info.roundTripMs + ms(_info.outputLookaheadFrames)).toStringAsFixed(1)} ms '
          '(${latencySourceLabel(_info.latencySource)}).',
          style: theme.textTheme.bodySmall,
        ),
        const SizedBox(height: 8),
        Text('Cắt đầu / đuôi', style: theme.textTheme.titleMedium),
        RangeSlider(
          values: RangeValues(
            trim.start.clamp(0, totalMs),
            trim.end.clamp(0, totalMs),
          ),
          max: totalMs > 0 ? totalMs : 1,
          onChanged: (v) => setState(() => _trimDrag = v),
          onChangeEnd: (v) {
            _trimDrag = null;
            final start = (v.start * rate / 1000).round();
            var end = (v.end * rate / 1000).round();
            if (end >= _info.frames) end = 0;
            _update(_info.copyWith(trimStartFrame: start, trimEndFrame: end));
          },
        ),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              formatDuration(Duration(milliseconds: trim.start.round())),
              style: theme.textTheme.bodySmall,
            ),
            Text(
              'Dài ${formatDuration(Duration(milliseconds: (trim.end - trim.start).round()))}',
              style: theme.textTheme.bodySmall,
            ),
            Text(
              formatDuration(Duration(milliseconds: trim.end.round())),
              style: theme.textTheme.bodySmall,
            ),
          ],
        ),
        const Divider(height: 32),
        // ---- Chuỗi giọng ----
        FxPanel(
          genre: _service.genre,
          preset: _service.presetOf(_service.genre),
          source: null,
          bpm: _info.tempoBpm,
          mix: _service.fxMix,
          gateOn: _service.gateOn,
          monitorOn: true,
          onGenre: (g) {
            _service.setGenre(g);
            _syncVoice();
          },
          onMix: (v) {
            _service.setFxMix(v);
            _syncVoice();
          },
          onGate: (v) {
            _service.setGate(v);
            _syncVoice();
          },
        ),
        const SizedBox(height: 12),
        Text('Tự chỉnh giọng', style: theme.textTheme.titleMedium),
        const SizedBox(height: 4),
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
          onSelectionChanged: (sel) {
            final v = sel.single;
            _service.setCorrection(v < 0 ? null : CorrectionPreset.values[v]);
            _syncVoice();
          },
        ),
        const Divider(height: 32),
        // ---- Xuất ----
        Text('Xuất', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        FilledButton.icon(
          onPressed: rendering || _loading ? null : _export,
          icon: const Icon(Icons.audio_file),
          label: const Text('Xuất âm thanh (M4A)'),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        ),
        if (rendering) ...[
          const SizedBox(height: 8),
          LinearProgressIndicator(value: _render?.progress),
          TextButton(
            onPressed: () => _renderer?.cancel(),
            child: const Text('Huỷ'),
          ),
        ],
        if (_exportError != null)
          Text(_exportError!, style: TextStyle(color: theme.colorScheme.error)),
        if (_exportPath != null && _render != null)
          _ExportResult(path: _exportPath!, status: _render!, onShare: _share),
      ],
    );
  }
}

class _ExportResult extends StatelessWidget {
  const _ExportResult({
    required this.path,
    required this.status,
    required this.onShare,
  });

  final String path;
  final RenderStatus status;
  final VoidCallback onShare;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final size = File(path).existsSync() ? File(path).lengthSync() : 0;
    final lufs = status.outputLufs;
    return Card(
      margin: const EdgeInsets.only(top: 8),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Đã xuất', style: theme.textTheme.titleMedium),
            Text(
              '${formatDuration(status.duration)} · ${formatBytes(size)}'
              '${lufs == null ? '' : ' · ${lufs.toStringAsFixed(1).replaceAll('.', ',')} LUFS'}',
            ),
            const SizedBox(height: 8),
            FilledButton.tonalIcon(
              onPressed: onShare,
              icon: const Icon(Icons.ios_share),
              label: const Text('Chia sẻ'),
            ),
          ],
        ),
      ),
    );
  }
}
