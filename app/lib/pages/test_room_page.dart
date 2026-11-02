import 'dart:async';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/song_import.dart';
import '../data/song_library.dart';
import '../data/song_ref.dart';
import '../data/take_store.dart';
import '../engine/labels.dart';
import '../engine/playback_watch.dart';
import '../engine/sing_controller.dart';
import '../engine/transpose_timing.dart';
import '../widgets/fx_panel.dart';
import '../widgets/gain_slider.dart';
import '../widgets/pitch_meter.dart';
import '../widgets/separation_panel.dart';
import '../widgets/transpose_control.dart';
import 'review_page.dart';

/// Phòng thử (M1–M7; M8: vào Cài đặt → Nâng cao): mọi nút và số đo của một phiên hát trên một
/// trang dài — nhạc, đổi tone (nhớ theo bài), tách giọng ca sĩ, nghe lại giọng, thể loại, chỉnh
/// giọng theo tone của bài, đồng hồ cao độ, thu âm + camera, thông số kỹ thuật. Logic nằm ở
/// [SingController] (dùng chung với Màn hát).
class TestRoomPage extends StatefulWidget {
  const TestRoomPage({super.key, this.initialSong, this.controller});

  /// Bài mở sẵn.
  final SongRef? initialSong;

  /// Bộ điều khiển phiên hát (test truyền vào; null = tạo mới, huỷ khi rời màn).
  final SingController? controller;

  @override
  State<TestRoomPage> createState() => _TestRoomPageState();
}

class _TestRoomPageState extends State<TestRoomPage> {
  late final SingController _c = widget.controller ?? SingController();
  double? _dragPosition;
  bool _pickError = false; // file vừa chọn không thêm được vào thư viện

  @override
  void initState() {
    super.initState();
    _c.onTake = _review;
    unawaited(_c.start(initial: widget.initialSong));
  }

  @override
  void dispose() {
    _c.onTake = null;
    if (widget.controller == null) _c.dispose();
    super.dispose();
  }

  Future<void> _review(Take take) async {
    if (!mounted) return;
    await Navigator.of(context)
        .push(MaterialPageRoute<void>(builder: (_) => ReviewPage(take: take)));
  }

  Future<void> _pickBeat() async {
    if (_c.engine == null) return;
    // Bài chọn ở đây cũng vào thư viện (hiện ở "Bài hát của tôi").
    final LibrarySong? song;
    try {
      song = await pickSongIntoLibrary(_c.library);
    } catch (_) {
      _c.clearMessage();
      setState(() => _pickError = true);
      return;
    }
    if (song == null) return; // người dùng huỷ
    setState(() => _pickError = false);
    await _c.openSong(song.ref);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Phòng thử')),
      body: ListenableBuilder(
        listenable: _c,
        builder: (context, _) => _c.starting
            ? const Center(child: CircularProgressIndicator())
            : _c.engine == null
            ? _ErrorView(message: _c.startError ?? 'Không mở được âm thanh')
            : _buildBody(context),
      ),
    );
  }

  Widget _buildBody(BuildContext context) {
    final c = _c;
    final pb = c.playback;
    final m = c.metrics;
    final theme = Theme.of(context);
    final duration = pb?.duration ?? Duration.zero;
    final position = pb?.position ?? Duration.zero;
    final maxMs = duration.inMilliseconds.toDouble();
    final speakerWarning = c.monitorOn && m?.route == AudioRoute.speaker;
    final message = _pickError
        ? 'Không thêm được file này. Hãy thử MP3, M4A hoặc WAV.'
        : c.message;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        if (message != null)
          Card(
            color: theme.colorScheme.errorContainer,
            child: ListTile(
              leading: const Icon(Icons.info_outline),
              title: Text(message),
              trailing: IconButton(
                icon: const Icon(Icons.close),
                onPressed: () {
                  setState(() => _pickError = false);
                  c.clearMessage();
                },
              ),
            ),
          ),
        // ---- Nhạc ----
        Text('Nhạc beat', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        FilledButton.icon(
          onPressed: c.loading || c.recording ? null : _pickBeat,
          icon: c.loading
              ? const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Icon(Icons.library_music),
          label: Text(c.song?.name ?? 'Chọn file nhạc'),
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
              c.seek(Duration(milliseconds: v.round()));
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
              onPressed: c.playPause,
              icon: Icon(p.playing ? Icons.pause : Icons.play_arrow),
            ),
          ),
          const SizedBox(height: 8),
          if (c.camera.supported)
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Quay video (camera trước)'),
              subtitle: const Text('Xuất video 9:16 có mặt bạn, khớp tiếng'),
              value: c.cameraOn,
              onChanged: c.recording ? null : c.setCamera,
            ),
          if (c.cameraOn)
            Center(
              child: ClipRRect(
                borderRadius: BorderRadius.circular(12),
                child: const SizedBox(
                  height: 220,
                  child: AspectRatio(
                    aspectRatio: 9 / 16,
                    child: CameraPreviewView(),
                  ),
                ),
              ),
            ),
          const SizedBox(height: 8),
          _RecordRow(
            recording: c.recording,
            countdown: c.countdown,
            elapsed: c.recStatus?.duration ?? Duration.zero,
            bluetooth: m?.route == AudioRoute.bluetooth,
            enabled: !c.loading && c.sepProgress == null,
            onStart: c.startRecording,
            onStop: c.stopRecording,
          ),
          const SizedBox(height: 8),
          TransposeControl(
            value: p.transpose,
            audible: p.audibleTranspose,
            progress: p.transposeProgress,
            onChanged: c.setTranspose,
            enabled: !c.recording,
          ),
        ],
        if (pb case final p? when p.hasBeat) ...[
          const SizedBox(height: 8),
          SeparationPanel(
            hasServer: c.server != null,
            hasStems: c.hasStems,
            progress: c.sepProgress,
            error: c.sepError,
            lastResult: c.sepResult,
            vocalGain: c.service.vocalGain,
            onSeparate: () =>
                c.separate(askConsent: () => askSeparationConsent(context)),
            onCancel: c.cancelSeparation,
            onDelete: c.deleteStems,
            onVocalGain: c.setVocalGain,
          ),
          const SizedBox(height: 8),
        ],
        GainSlider(
          label: 'Âm lượng nhạc',
          value: c.beatGain,
          onChanged: c.setBeatGain,
        ),
        const Divider(height: 32),
        // ---- Nghe lại giọng ----
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: Text('Nghe lại giọng', style: theme.textTheme.titleLarge),
          subtitle: const Text('Nghe giọng mình qua tai nghe khi hát'),
          value: c.monitorOn,
          onChanged: c.setMonitorOn,
        ),
        if (speakerWarning)
          Text(
            'Đang dùng loa ngoài: dễ bị hú. Hãy cắm tai nghe có dây.',
            style: TextStyle(color: theme.colorScheme.error),
          ),
        GainSlider(
          label: 'Âm lượng giọng',
          value: c.monitorGain,
          max: 2,
          onChanged: c.setMonitorGain,
        ),
        const Divider(height: 32),
        // ---- Hiệu ứng giọng ----
        FxPanel(
          genre: c.service.genre,
          preset: c.service.presetOf(c.service.genre),
          source: c.genreSource,
          bpm: c.analysis?.bpm,
          mix: c.service.fxMix,
          gateOn: c.service.gateOn,
          monitorOn: c.monitorOn,
          onGenre: c.pickGenre,
          onMix: c.setFxMix,
          onGate: c.setGate,
        ),
        const Divider(height: 32),
        // ---- Tự chỉnh giọng ----
        Text('Tự chỉnh giọng', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        if (c.analyzing) _AnalysisProgress(status: c.analysisStatus),
        if (c.analysisError != null)
          Text(
            c.analysisError!,
            style: TextStyle(color: theme.colorScheme.error),
          ),
        if (!c.analyzing && c.analysis != null)
          _AnalysisCard(
            analysis: c.analysis!,
            follow: c.service.followSong,
            onFollowChanged: c.setFollowSong,
          ),
        const SizedBox(height: 8),
        _KeyPicker(
          value: c.currentKey,
          transpose: pb?.audibleTranspose ?? 0,
          followingSong: c.service.followSong,
          onChanged: c.setKey,
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
          selected: {c.service.correction?.index ?? -1},
          showSelectedIcon: false,
          onSelectionChanged: (sel) {
            final v = sel.single;
            c.setCorrection(v < 0 ? null : CorrectionPreset.values[v]);
          },
        ),
        const SizedBox(height: 4),
        Text(
          c.service.correction == null
              ? 'Đồng hồ cao độ vẫn hoạt động khi tắt.'
              : c.monitorOn
              ? 'Giọng nghe lại trễ thêm 10 ms khi bật tự chỉnh giọng.'
              : 'Bật "Nghe lại giọng" để nghe giọng đã chỉnh.',
          style: theme.textTheme.bodySmall,
        ),
        const SizedBox(height: 8),
        ValueListenableBuilder(
          valueListenable: c.pitch,
          builder: (context, pitch, _) => PitchMeter(state: pitch),
        ),
        const Divider(height: 32),
        // ---- Máy chủ tách giọng (bản phát triển) ----
        ServerSettingsCard(initial: c.server, onSave: c.saveServer),
        const SizedBox(height: 8),
        // ---- Số đo ----
        if (m != null)
          _MetricsCard(
            metrics: m,
            timing: c.timing,
            watch: playbackWatchLabel(c.watch, c.clock),
            analysisTime: c.analysisTime,
            rssMb: c.rssMb,
            maxRssMb: c.maxRssMb,
          ),
      ],
    );
  }
}

/// Nút Thu âm / Dừng + đồng hồ (M7).
class _RecordRow extends StatelessWidget {
  const _RecordRow({
    required this.recording,
    required this.countdown,
    required this.elapsed,
    required this.bluetooth,
    required this.enabled,
    required this.onStart,
    required this.onStop,
  });

  final bool recording;
  final int? countdown;
  final Duration elapsed;
  final bool bluetooth;
  final bool enabled;
  final VoidCallback onStart;
  final VoidCallback onStop;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final red = theme.colorScheme.error;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (recording)
          FilledButton.icon(
            onPressed: onStop,
            icon: const Icon(Icons.stop),
            label: Text('Dừng · ${formatDuration(elapsed)}'),
            style: FilledButton.styleFrom(
              backgroundColor: red,
              foregroundColor: theme.colorScheme.onError,
              minimumSize: const Size.fromHeight(56),
            ),
          )
        else
          OutlinedButton.icon(
            onPressed: enabled && countdown == null ? onStart : null,
            icon: Icon(Icons.fiber_manual_record, color: red),
            label: Text(countdown != null ? 'Chuẩn bị… $countdown' : 'Thu âm'),
            style: OutlinedButton.styleFrom(
              minimumSize: const Size.fromHeight(56),
            ),
          ),
        if (bluetooth)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              'Tai nghe Bluetooth trễ nhiều: giọng thu có thể lệch nhạc. Dùng tai nghe có dây để '
              'khớp nhất.',
              style: theme.textTheme.bodySmall?.copyWith(color: red),
            ),
          ),
      ],
    );
  }
}

/// Đang phân tích bài: giai đoạn + thanh tiến độ.
class _AnalysisProgress extends StatelessWidget {
  const _AnalysisProgress({required this.status});

  final AnalysisStatus? status;

  @override
  Widget build(BuildContext context) {
    final s = status;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(analysisStageLabel(s?.stage ?? AnalysisStage.none)),
          const SizedBox(height: 6),
          LinearProgressIndicator(value: s?.progress),
        ],
      ),
    );
  }
}

/// Kết quả phân tích: "Tone: La thứ (Am) – 92 BPM", chuyển tone, tuning, công tắc theo bài.
class _AnalysisCard extends StatelessWidget {
  const _AnalysisCard({
    required this.analysis,
    required this.follow,
    required this.onFollowChanged,
  });

  final SongAnalysis analysis;
  final bool follow;
  final ValueChanged<bool> onFollowChanged;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final modulation = modulationSummary(analysis);
    final tuning = tuningNote(analysis);
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Bài này', style: theme.textTheme.labelLarge),
            Text(
              analysisSummary(analysis),
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w700,
              ),
            ),
            if (modulation != null) Text('Chuyển tone: $modulation'),
            if (tuning != null) Text(tuning, style: theme.textTheme.bodySmall),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Tone theo bài'),
              subtitle: Text(
                follow
                    ? 'Tự chỉnh giọng theo tone của bài, tự đổi khi bài chuyển tone'
                    : 'Đang dùng tone chọn tay',
              ),
              value: follow,
              onChanged: analysis.key == null ? null : onFollowChanged,
            ),
          ],
        ),
      ),
    );
  }
}

/// Tone đang áp dụng (thật to) + chọn tay: chủ âm (12 nốt) + thang âm.
class _KeyPicker extends StatelessWidget {
  const _KeyPicker({
    required this.value,
    required this.transpose,
    required this.followingSong,
    required this.onChanged,
  });

  /// Tone đang nghe (đã cộng [transpose]).
  final MusicKey value;
  final int transpose;
  final bool followingSong;
  final ValueChanged<MusicKey> onChanged;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'Tone: ${keyLabel(value)}',
          style: theme.textTheme.headlineMedium?.copyWith(
            fontWeight: FontWeight.w700,
          ),
        ),
        Text(
          [
            followingSong ? 'Theo bài · chọn bên dưới để đổi tay' : 'Chọn tay',
            if (transpose != 0) 'beat ${transposeLabel(transpose)}',
          ].join(' · '),
          style: theme.textTheme.bodySmall,
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

class _MetricsCard extends StatelessWidget {
  const _MetricsCard({
    required this.metrics,
    required this.timing,
    required this.watch,
    required this.analysisTime,
    required this.rssMb,
    required this.maxRssMb,
  });

  final EngineMetrics metrics;
  final TransposeTiming timing;
  final String watch;
  final Duration? analysisTime;
  final int rssMb;
  final int maxRssMb;

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
      (
        'FX giọng',
        '${(m.fxLoad * 100).toStringAsFixed(1)} % CPU · ${m.fxChains} chuỗi',
      ),
      ('Trễ xử lý', '${m.algorithmLatencyMs.toStringAsFixed(1)} ms'),
      ('Xrun (rè)', '${m.xruns}'),
      ('Đổi tone beat', transposeTimingLabel(timing)),
      ('Sau khi đổi tone', watch),
      (
        'Phân tích bài',
        analysisTime == null
            ? '—'
            : '${(analysisTime!.inMilliseconds / 1000).toStringAsFixed(1)} s',
      ),
      ('RAM', '$rssMb MB (đỉnh $maxRssMb MB)'),
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
