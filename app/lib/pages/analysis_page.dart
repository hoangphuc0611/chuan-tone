import 'dart:async';

import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/analysis_cache.dart';
import '../data/app_settings.dart';
import '../data/song_prefs.dart';
import '../data/song_ref.dart';
import '../data/stem_store.dart';
import '../data/test_log.dart';
import '../engine/labels.dart';
import '../engine/separation_client.dart';
import '../engine/song_jobs.dart';
import '../widgets/analysis_widgets.dart';
import '../widgets/separation_panel.dart';
import 'sing_page.dart';

/// Màn Phân tích bài (CLAUDE.md mục 5 màn 2, M6): các bước (tách giọng ca sĩ → dò tone → dò nhịp),
/// kết quả "Tone: La thứ (Am) – 92 BPM", sửa tone tay (nhớ theo bài), nút "Hát".
///
/// Phân tích bài gốc chạy ngay (≈ 1 s). Tách giọng chỉ khi người dùng bấm (bài gốc có giọng ca sĩ;
/// beat karaoke không cần) và đồng ý gửi bài; tách xong thì phân tích lại trên phần nhạc.
class AnalysisPage extends StatefulWidget {
  const AnalysisPage({super.key, required this.song});

  final SongRef song;

  @override
  State<AnalysisPage> createState() => _AnalysisPageState();
}

class _AnalysisPageState extends State<AnalysisPage> {
  final _settings = AppSettings();
  final _stems = StemStore();
  final _prefs = SongPrefs();
  late final _cache = AnalysisCache(engineVersion: ToneCore.version);

  SongAnalysis? _analysis;
  AnalysisStatus? _status;
  bool _analyzing = false;
  String? _analysisError;
  SongAnalyzer? _analyzer;
  MusicKey? _manualKey;

  String? _server;
  List<String>? _vocals;
  SeparationProgress? _sepProgress;
  SeparationCancel? _sepCancel;
  SeparationResult? _sepResult;
  String? _sepError;

  SongRef get _song => widget.song;

  @override
  void initState() {
    super.initState();
    TestLog.screen('analysis');
    _init();
  }

  Future<void> _init() async {
    final server = await _settings.separationServer();
    final vocals = await _stems.vocalsFor(name: _song.name, size: _song.size);
    final last = vocals == null
        ? null
        : await _stems.resultFor(name: _song.name, size: _song.size);
    final manual = await _prefs.keyFor(name: _song.name, size: _song.size);
    if (!mounted) return;
    setState(() {
      _server = server;
      _vocals = vocals;
      _sepResult = last;
      _manualKey = manual;
    });
    await _analyze();
  }

  Future<void> _analyze() async {
    _analyzer?.dispose();
    setState(() {
      _analyzing = true;
      _analysisError = null;
      _status = null;
    });
    try {
      final (r, _) = await analyzeSong(
        _song,
        vocals: _vocals,
        cache: _cache,
        onAnalyzer: (a) => _analyzer = a,
        onProgress: (s) {
          if (mounted) setState(() => _status = s);
        },
      );
      _analysis = r;
    } on StateError {
      return; // màn đã đóng / phân tích lại: analyzer bị dispose
    } on ToneCoreException catch (err) {
      _analysisError = err.code == TcResult.cancelled
          ? null
          : 'Chưa dò được tone của bài. Bạn vẫn hát được và sửa tone tay được.';
    } on TimeoutException {
      _analysisError = 'Phân tích bài quá lâu.';
    } finally {
      _analyzer = null;
    }
    if (mounted) setState(() => _analyzing = false);
  }

  Future<void> _separate() async {
    final server = _server;
    if (server == null) return;
    if (!await _settings.separationConsent()) {
      if (!mounted) return;
      final remember = await askSeparationConsent(context);
      if (remember == null) return;
      if (remember) await _settings.setSeparationConsent(true);
    }
    final cancel = _sepCancel = SeparationCancel();
    setState(() {
      _sepError = null;
      _sepProgress = SeparationProgress.start;
    });
    try {
      final r = await separateSong(
        _song,
        server: server,
        settings: _settings,
        store: _stems,
        cancel: cancel,
        onProgress: (p) {
          if (mounted) setState(() => _sepProgress = p);
        },
      );
      if (!mounted) return;
      _vocals = [for (final f in r.vocals) f.path];
      _sepResult = r;
      unawaited(_analyze()); // phân tích lại trên phần nhạc
    } on SeparationException catch (err) {
      _sepError = err.message;
    } finally {
      if (mounted) {
        setState(() {
          _sepProgress = null;
          _sepCancel = null;
        });
      }
    }
  }

  Future<void> _editKey() async {
    final detected = _analysis?.key;
    final edit = await showKeyEditor(
      context,
      initial:
          _manualKey ?? detected ?? const MusicKey(9, MusicScale.naturalMinor),
      canReset: _manualKey != null,
    );
    if (edit == null) return;
    final key = edit.reset ? null : edit.key;
    await _prefs.setKey(name: _song.name, size: _song.size, key: key);
    if (mounted) setState(() => _manualKey = key);
  }

  @override
  void dispose() {
    _analyzer?.dispose();
    _sepCancel?.cancel();
    super.dispose();
  }

  List<AnalysisStep> _steps() {
    final p = _sepProgress;
    final stage = _status?.stage ?? AnalysisStage.none;
    final tempoRunning = _analyzing && stage == AnalysisStage.tempo;
    // Dò nhịp chạy sau khi đã dò xong tone.
    final toneDone = _analysis != null || tempoRunning;
    return [
      AnalysisStep(
        title: 'Tách giọng ca sĩ',
        state: p != null
            ? StepStatus.running
            : _vocals != null
            ? StepStatus.done
            : _sepError != null
            ? StepStatus.failed
            : StepStatus.skipped,
        subtitle: p != null
            ? separationStageLabel(p)
            : _vocals != null
            ? (_sepResult != null
                  ? separationDoneLabel(_sepResult!)
                  : 'Đã tách — dò tone trên phần nhạc')
            : _sepError ??
                  (_server == null
                      ? 'Không bắt buộc. Cần máy chủ tách giọng (nhập ở Cài đặt → Nâng cao).'
                      : 'Không bắt buộc — dùng cho bài gốc có giọng ca sĩ.'),
        progress: p?.fraction,
        action: p != null
            ? TextButton(
                onPressed: () => _sepCancel?.cancel(),
                child: const Text('Huỷ'),
              )
            : _vocals == null && _server != null
            ? FilledButton.tonal(
                onPressed: _analyzing ? null : _separate,
                child: const Text('Tách'),
              )
            : null,
      ),
      AnalysisStep(
        title: 'Dò tone',
        state: _analysisError != null
            ? StepStatus.failed
            : _analyzing && !tempoRunning
            ? StepStatus.running
            : toneDone
            ? StepStatus.done
            : StepStatus.waiting,
        subtitle:
            _analysisError ??
            (_analyzing && !tempoRunning ? analysisStageLabel(stage) : null),
        progress: _status?.progress,
      ),
      AnalysisStep(
        title: 'Dò nhịp',
        state: tempoRunning
            ? StepStatus.running
            : _analysis != null && !_analyzing
            ? StepStatus.done
            : StepStatus.waiting,
        progress: _status?.progress,
      ),
    ];
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final a = _analysis;
    final busy = _analyzing || _sepProgress != null;
    return Scaffold(
      appBar: AppBar(title: const Text('Phân tích bài')),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
        children: [
          Text(
            songTitle(_song.name),
            style: theme.textTheme.headlineSmall,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          ),
          const SizedBox(height: 12),
          AnalysisSteps(steps: _steps()),
          if (a != null) ...[
            const SizedBox(height: 4),
            KeyResultCard(analysis: a, manualKey: _manualKey, onEdit: _editKey),
          ],
        ],
      ),
      // Nút chính luôn ở đáy màn.
      bottomNavigationBar: SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 8),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (_sepProgress != null)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Text(
                    'Giữ màn hình mở trong lúc tách giọng.',
                    style: theme.textTheme.bodySmall,
                    textAlign: TextAlign.center,
                  ),
                ),
              FilledButton.icon(
                onPressed: busy && a == null
                    ? null
                    : () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => SingPage(song: _song),
                        ),
                      ),
                icon: const Icon(Icons.mic),
                label: const Text('Hát'),
                style: FilledButton.styleFrom(
                  minimumSize: const Size.fromHeight(60),
                  textStyle: theme.textTheme.titleLarge,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
