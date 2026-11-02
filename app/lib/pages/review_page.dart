import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
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

  // Xuất: file M4A dùng lại cho video nếu thông số chưa đổi (_audioKey).
  TakeRenderer? _renderer;
  bool _videoBusy = false;
  double? _progress;
  String? _busyLabel;
  RenderStatus? _audioStatus;
  String? _audioPath;
  String? _audioKey;
  String? _videoPath;
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

  String _stamp() {
    final t = DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    return '${two(t.hour)}${two(t.minute)}${two(t.second)}';
  }

  /// File M4A của lựa chọn hiện tại (render nếu chưa có / thông số đã đổi). null = lỗi / huỷ.
  Future<String?> _ensureAudio() async {
    final key = jsonEncode(_info.toJson());
    final cached = _audioPath;
    if (cached != null && _audioKey == key && File(cached).existsSync()) {
      return cached;
    }
    final r = _renderer = TakeRenderer.create();
    final out = '${_take.dir.path}/xuat-${_stamp()}.m4a';
    setState(() {
      _exportError = null;
      _busyLabel = 'Đang trộn âm thanh…';
      _progress = 0;
    });
    try {
      final s = await r.render(
        _take.beatPath,
        _take.dryPath,
        out,
        renderSettingsFor(_info, preset: _service.presetOf(_info.genre)),
        onProgress: (s) {
          if (mounted) setState(() => _progress = s.progress);
        },
      );
      if (cached != null && cached != out) {
        unawaited(File(cached).delete().then((_) {}, onError: (_) {}));
      }
      _audioPath = out;
      _audioKey = key;
      _audioStatus = s;
      return out;
    } on ToneCoreException catch (err) {
      if (err.code != TcResult.cancelled) {
        _exportError = 'Xuất lỗi (${err.codeName}).';
      }
      return null;
    } finally {
      r.dispose();
      _renderer = null;
      if (mounted) setState(() => _busyLabel = null);
    }
  }

  Future<void> _exportAudio() async {
    await _ensureAudio();
    if (mounted) setState(() {});
  }

  /// Video camera trước (B6): ghép hình đã quay với file M4A theo mốc đồng hồ máy.
  Future<void> _exportCameraVideo() async {
    final video = _take.videoPath;
    if (video == null) return;
    final audio = await _ensureAudio();
    if (audio == null || !mounted) return;
    final out = '${_take.dir.path}/video-${_stamp()}.mp4';
    setState(() {
      _videoBusy = true;
      _busyLabel = 'Đang ghép video…';
      _progress = null;
      _exportError = null;
    });
    try {
      await ChuanToneMedia.muxVideo(
        video: video,
        audio: audio,
        output: out,
        offsetNs: cameraOffsetNs(_info),
      );
      final old = _videoPath;
      if (old != null) {
        unawaited(File(old).delete().then((_) {}, onError: (_) {}));
      }
      _videoPath = out;
    } on PlatformException catch (err) {
      _exportError = 'Ghép video lỗi: ${err.message ?? err.code}';
    } on MissingPluginException {
      _exportError = 'Máy này chưa ghép được video.';
    } finally {
      if (mounted) {
        setState(() {
          _videoBusy = false;
          _busyLabel = null;
        });
      }
    }
  }

  /// Video 9:16 nền sóng nhạc + tên bài (iOS, B5): dựng từ chính file M4A nên khớp tiếng.
  Future<void> _exportVideo() async {
    final audio = await _ensureAudio();
    if (audio == null || !mounted) return;
    final out = '${_take.dir.path}/video-${_stamp()}.mp4';
    setState(() {
      _videoBusy = true;
      _busyLabel = 'Đang dựng video…';
      _progress = 0;
      _exportError = null;
    });
    final poll = Timer.periodic(const Duration(milliseconds: 250), (_) async {
      final p = await ChuanToneMedia.progress();
      if (mounted && _videoBusy) setState(() => _progress = p);
    });
    try {
      final ok = await ChuanToneMedia.renderWaveformVideo(
        audio: audio,
        output: out,
        title: songTitle(_info.songName),
        subtitle:
            '${genreLabel(_info.genre)} · ${formatTakeDate(_info.createdAt)}',
        color: genreColor(_info.genre),
      );
      if (ok == true) {
        final old = _videoPath;
        if (old != null) {
          unawaited(File(old).delete().then((_) {}, onError: (_) {}));
        }
        _videoPath = out;
      } else if (ok == null) {
        _exportError = 'Máy này chưa dựng được video (sẽ có trên Android sau).';
      }
    } on PlatformException catch (err) {
      _exportError = 'Dựng video lỗi: ${err.message ?? err.code}';
    } finally {
      poll.cancel();
      if (mounted) {
        setState(() {
          _videoBusy = false;
          _busyLabel = null;
        });
      }
    }
  }

  void _cancelExport() {
    _renderer?.cancel();
    if (_videoBusy) unawaited(ChuanToneMedia.cancel());
  }

  Future<void> _share(String path) async {
    final ok = await ChuanToneMedia.share(path);
    if (ok == null && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Máy này chưa chia sẻ được. File: $path')),
      );
    }
  }

  Future<void> _saveVideo(String path) async {
    String msg;
    try {
      final ok = await ChuanToneMedia.saveVideoToPhotos(path);
      msg = switch (ok) {
        true => 'Đã lưu video vào Ảnh.',
        false => 'Chưa được phép lưu vào Ảnh (Cài đặt → ChuẩnTone → Ảnh).',
        null => 'Máy này chưa lưu được vào Ảnh.',
      };
    } on PlatformException catch (err) {
      msg = 'Không lưu được: ${err.message ?? err.code}';
    }
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
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
    _cancelExport();
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
    final busy = _busyLabel != null;

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
        if (_take.videoPath != null) ...[
          FilledButton.icon(
            onPressed: busy || _loading ? null : _exportCameraVideo,
            icon: const Icon(Icons.videocam),
            label: const Text('Xuất video camera (9:16)'),
            style: FilledButton.styleFrom(
              minimumSize: const Size.fromHeight(56),
            ),
          ),
          const SizedBox(height: 8),
        ],
        FilledButton.icon(
          onPressed: busy || _loading ? null : _exportVideo,
          icon: const Icon(Icons.movie),
          label: const Text('Xuất video sóng nhạc (9:16)'),
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        ),
        const SizedBox(height: 8),
        OutlinedButton.icon(
          onPressed: busy || _loading ? null : _exportAudio,
          icon: const Icon(Icons.audio_file),
          label: const Text('Xuất âm thanh (M4A)'),
          style: OutlinedButton.styleFrom(
            minimumSize: const Size.fromHeight(48),
          ),
        ),
        if (busy) ...[
          const SizedBox(height: 8),
          Text(_busyLabel!),
          const SizedBox(height: 4),
          LinearProgressIndicator(value: _progress),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton(
              onPressed: _cancelExport,
              child: const Text('Huỷ'),
            ),
          ),
        ],
        if (_exportError != null)
          Text(_exportError!, style: TextStyle(color: theme.colorScheme.error)),
        if (_videoPath case final v? when !busy)
          _ExportResult(
            title: 'Video 9:16',
            path: v,
            status: _audioStatus,
            onShare: () => _share(v),
            onSave: () => _saveVideo(v),
          ),
        if (_audioPath case final a? when !busy)
          _ExportResult(
            title: 'Âm thanh M4A',
            path: a,
            status: _audioStatus,
            onShare: () => _share(a),
          ),
      ],
    );
  }
}

class _ExportResult extends StatelessWidget {
  const _ExportResult({
    required this.title,
    required this.path,
    required this.status,
    required this.onShare,
    this.onSave,
  });

  final String title;
  final String path;
  final RenderStatus? status;
  final VoidCallback onShare;
  final VoidCallback? onSave;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final size = File(path).existsSync() ? File(path).lengthSync() : 0;
    final lufs = status?.outputLufs;
    return Card(
      margin: const EdgeInsets.only(top: 8),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Đã xuất: $title', style: theme.textTheme.titleMedium),
            Text(
              [
                if (status != null) formatDuration(status!.duration),
                formatBytes(size),
                if (lufs != null)
                  '${lufs.toStringAsFixed(1).replaceAll('.', ',')} LUFS',
              ].join(' · '),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                if (onSave != null)
                  FilledButton.icon(
                    onPressed: onSave,
                    icon: const Icon(Icons.photo_library),
                    label: const Text('Lưu vào Ảnh'),
                  ),
                FilledButton.tonalIcon(
                  onPressed: onShare,
                  icon: const Icon(Icons.ios_share),
                  label: const Text('Chia sẻ'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
