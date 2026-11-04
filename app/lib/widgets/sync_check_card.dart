import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/wav_io.dart';
import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../engine/sync_check.dart';
import '../engine/take_alignment.dart';

/// Kiểm tra khớp tiếng khi thu (M7 B2): phát beat toàn tiếng "tách", thu 6 s qua micro (loa →
/// micro), đo giọng khô chậm hơn beat bao nhiêu và so với độ lệch app sẽ bù (độ trễ khứ hồi +
/// limiter nhìn trước, docs/adr/007-recording-sync.md). Chỉ tiêu: lệch còn ≤ 5 ms.
class SyncCheckCard extends StatefulWidget {
  const SyncCheckCard({super.key, required this.history, this.model = ''});

  /// Lịch sử đo độ trễ (dòng CSV, mới nhất trước).
  final List<String> history;
  final String model;

  @override
  State<SyncCheckCard> createState() => _SyncCheckCardState();
}

class _SyncCheckCardState extends State<SyncCheckCard> {
  static const _length = Duration(seconds: 6);

  bool _running = false;
  String? _error;
  String? _result;
  String? _csv;

  Future<void> _run() async {
    final e = AudioService.instance.engine;
    if (e == null) return;
    setState(() {
      _running = true;
      _error = null;
      _result = null;
    });
    final dir = await Directory.systemTemp.createTemp('khop_tieng');
    final monitor = e.monitorEnabled;
    try {
      final rate = e.metrics.sampleRate;
      final mono = clickTrack(sampleRate: rate, length: _length * 1.5);
      final stereo = Float32List(mono.length * 2);
      for (var i = 0; i < mono.length; i++) {
        stereo[2 * i] = mono[i];
        stereo[2 * i + 1] = mono[i];
      }
      final beatPath = '${dir.path}/tach.wav';
      await File(beatPath)
          .writeAsBytes(encodeWav16(stereo, sampleRate: rate, channels: 2));
      AudioService.instance.loads++;
      await e.loadBeatAndWait(beatPath);
      e.monitorEnabled = false; // không để micro vọng lại
      e.seek(Duration.zero);
      e.play();
      e.startRecording(dir.path, maxDuration: _length);
      while (e.recordStatus.state == RecordState.recording) {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      }
      e.stopRecording();
      e.pause();
      final s = e.recordStatus;
      final beat = readWavFile('${dir.path}/beat.wav').channel(0);
      final dry = readWavFile('${dir.path}/dry.wav').channel(0);
      final m = measureSync(beat: beat, dry: dry, sampleRate: s.sampleRate);
      if (m == null) {
        _error =
            'Micro không nghe rõ tiếng "tách". Tăng âm lượng ~70%, để nơi yên tĩnh '
            '(tai nghe có dây: áp một bên tai nghe sát micro).';
        return;
      }
      final rt = roundTripFor(
        route: s.route,
        reportedMs: s.inputLatencyMs + s.outputLatencyMs,
        history: parseLatencyHistory(widget.history),
      );
      final lookMs = s.outputLookaheadFrames * 1000 / s.sampleRate;
      final predicted = rt.ms + lookMs;
      final err = m.lagMs - predicted;
      final source = switch (rt.source) {
        LatencySource.measured => 'độ trễ đã đo',
        LatencySource.corrected => 'HĐH báo + chênh đo ở loa ngoài',
        LatencySource.reported => 'HĐH báo',
      };
      _result =
          '${routeLabel(s.route)}: giọng trong bản thu chậm ${m.lagMs.toStringAsFixed(1)} ms '
          '(${m.pairs} tiếng, dao động ${m.spreadMs.toStringAsFixed(1)} ms).\n'
          'App sẽ bù ${predicted.toStringAsFixed(1)} ms ($source '
          '${rt.ms.toStringAsFixed(1)} + limiter ${lookMs.toStringAsFixed(1)}) → '
          'còn lệch ${err.toStringAsFixed(1)} ms '
          '${err.abs() <= 5 ? '✓ đạt (≤ 5 ms)' : '✗ chưa đạt (> 5 ms)'}.';
      _csv = [
        DateTime.now().toIso8601String(),
        '"${widget.model}"',
        s.route.name,
        m.lagMs.toStringAsFixed(2),
        m.spreadMs.toStringAsFixed(2),
        m.pairs,
        rt.ms.toStringAsFixed(2),
        rt.source.name,
        lookMs.toStringAsFixed(2),
        err.toStringAsFixed(2),
        s.droppedFrames,
      ].join(',');
    } catch (err) {
      _error = 'Kiểm tra thất bại: $err';
    } finally {
      e.monitorEnabled = monitor;
      unawaited(dir.delete(recursive: true).then((_) {}, onError: (_) {}));
      if (mounted) setState(() => _running = false);
    }
  }

  Future<void> _copy() async {
    final csv = _csv;
    if (csv == null) return;
    await Clipboard.setData(
      ClipboardData(
        text:
            'date,model,route,lag_ms,spread_ms,clicks,round_trip_ms,source,'
            'lookahead_ms,error_ms,dropped\n$csv',
      ),
    );
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('Đã sao chép.')));
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Kiểm tra khớp tiếng khi thu',
              style: theme.textTheme.titleMedium,
            ),
            const SizedBox(height: 4),
            const Text(
              'Máy phát tiếng "tách" 6 giây và tự thu lại để xem giọng thu có khớp nhạc không. '
              'Loa ngoài: âm lượng ~70%. Tai nghe có dây: áp một bên tai nghe sát micro. '
              'Bài đang nạp sẽ được thay bằng tiếng "tách".',
            ),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: _running ? null : _run,
              icon: _running
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.graphic_eq),
              label: Text(_running ? 'Đang kiểm tra…' : 'Kiểm tra'),
            ),
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Text(
                  _error!,
                  style: TextStyle(color: theme.colorScheme.error),
                ),
              ),
            if (_result != null) ...[
              const SizedBox(height: 8),
              Text(_result!),
              Align(
                alignment: Alignment.centerRight,
                child: TextButton.icon(
                  onPressed: _copy,
                  icon: const Icon(Icons.copy),
                  label: const Text('Sao chép'),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
