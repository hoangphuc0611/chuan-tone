import 'dart:async';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/analysis_cache.dart';
import '../data/app_settings.dart';
import '../data/song_prefs.dart';
import '../data/stem_store.dart';
import '../engine/audio_service.dart';
import '../engine/genre_rules.dart';
import '../engine/labels.dart';
import '../engine/native_chunk_source.dart';
import '../engine/playback_watch.dart';
import '../engine/separation_client.dart';
import '../engine/transpose_timing.dart';
import '../widgets/fx_panel.dart';
import '../widgets/gain_slider.dart';
import '../widgets/pitch_meter.dart';
import '../widgets/separation_panel.dart';
import '../widgets/transpose_control.dart';

/// Màn thử M1–M6: phát beat, đổi tone beat (nhớ theo bài), tự dò tone / BPM khi nạp bài, tách giọng
/// ca sĩ qua máy chủ (nhớ theo bài, "Giữ giọng ca sĩ gốc"), nghe lại giọng, hiệu ứng giọng theo thể
/// loại (gợi ý + nhớ theo bài), tự chỉnh giọng theo tone của bài (tự cộng số nửa cung đổi tone),
/// đồng hồ cao độ, số đo.
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
  SongAnalyzer? _analyzer;
  AnalysisCache? _cache;
  bool _analyzing = false;
  AnalysisStatus? _analysisStatus;
  String? _analysisError;
  MusicKey? _currentKey;
  final _prefs = SongPrefs();
  // Số đo trên máy thật (M4 B7): bấm tone → nghe, render cả bài, thời gian phân tích, RAM.
  final _clock = Stopwatch()..start();
  final _timing = TransposeTiming();
  final _watch = PlaybackWatch();
  Duration? _analysisTime;
  int _rssMb = 0;
  int _maxRssMb = 0;
  String? _beatFile; // tên + kích thước của beat đang nạp: khoá nhớ tone / preset theo bài
  int? _beatSize;
  // Preset đang chọn có từ đâu (null: giữ preset chọn lần trước).
  GenreSource? _genreSource;
  // Tách giọng ca sĩ (M6).
  final _settings = AppSettings();
  final _stemStore = StemStore();
  String? _server;
  String? _beatPath;
  List<String>? _vocals; // các đoạn giọng của bài đang nạp (null = chưa tách)
  SeparationProgress? _sepProgress;
  SeparationCancel? _sepCancel;
  String? _sepError;
  SeparationResult? _sepResult;

  @override
  void initState() {
    super.initState();
    _start();
  }

  Future<void> _start() async {
    if (!_service.isRunning) {
      // Mở app: dùng lại preset chọn gần nhất (kèm mức chỉnh giọng của nó).
      final last = await _prefs.lastGenre();
      if (last != null) _service.setGenre(last);
    }
    _server = await _settings.separationServer();
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
      setState(() {
        _pitch = eng.pitchState;
        final now = _clock.elapsed;
        if (_timing.pending || !_watch.done(now)) {
          final pb = eng.playback;
          _watch.observe(at: now, position: pb.position, playing: pb.playing);
          if (_timing.observe(pb.audibleTranspose, now)) _playback = pb;
        }
      });
    });
  }

  void _refresh() {
    final e = _service.engine;
    if (e == null || !mounted) return;
    setState(() {
      _playback = e.playback;
      _metrics = e.metrics;
      _currentKey = _service.currentKey;
      _rssMb = ProcessInfo.currentRss >> 20;
      _maxRssMb = ProcessInfo.maxRss >> 20;
    });
  }

  void _onEvent(ToneCoreEvent ev) {
    if (!mounted) return;
    switch (ev.type) {
      case ToneCoreEventType.keyChanged:
        setState(() => _currentKey = ev.key);
      case ToneCoreEventType.transposeReady:
        setState(() => _timing.rendered(ev.code, ev.value));
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

  void _setTranspose(int semitones) {
    final e = _service.engine;
    if (e == null || !_service.setTranspose(semitones)) return;
    _timing.requested(semitones, _clock.elapsed);
    _watch.restart(_clock.elapsed);
    setState(() => _playback = e.playback);
    final name = _beatFile;
    final size = _beatSize;
    if (name != null && size != null) {
      unawaited(
        _prefs.setTranspose(name: name, size: size, semitones: semitones),
      );
    }
  }

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
      final size = await File(path).length();
      final vocals = await _loadSong(e, path, files.single.name, size);
      _beatName = files.single.name;
      _beatFile = files.single.name;
      _beatSize = size;
      _beatPath = path;
      _vocals = vocals;
      _sepError = null;
      _sepResult = null;
      _service.setVocalGain(0); // bài mới: karaoke
      // Nạp beat mới → engine về tone gốc; đặt lại tone người dùng đã chọn cho bài này.
      final saved = await _prefs.transposeFor(
        name: files.single.name,
        size: size,
      );
      _timing.reset();
      _analysisTime = null;
      if (saved != 0 && _service.setTranspose(saved)) {
        _timing.requested(saved, _clock.elapsed);
      }
      // Preset: đã chọn cho bài → theo tên file; theo BPM thì chờ phân tích xong.
      _applySuggestion(
        suggestGenre(
          saved: await _prefs.genreFor(name: files.single.name, size: size),
          fileName: files.single.name,
        ),
      );
      _playback = e.playback;
      unawaited(_analyze(path, files.single.name, size, vocals));
    } on ToneCoreException catch (err) {
      _show('Không mở được file (${err.codeName}). Hãy thử MP3, M4A hoặc WAV.');
    } on TimeoutException {
      _show('Nạp bài quá lâu.');
    }
    if (mounted) setState(() => _loading = false);
  }

  /// Dò tone / chuyển tone / tuning / BPM của beat vừa nạp (lấy từ bộ nhớ nếu đã phân tích), rồi
  /// cho tự chỉnh giọng theo bài.
  Future<void> _analyze(
    String path,
    String name,
    int size,
    List<String>? vocals,
  ) async {
    _analyzer?.dispose(); // beat trước còn đang phân tích → dừng
    _analyzer = null;
    _service.applySongAnalysis(null);
    setState(() {
      _analyzing = true;
      _analysisStatus = null;
      _analysisError = null;
    });
    final cache = _cache ??= AnalysisCache(engineVersion: ToneCore.version);
    // Đã tách giọng: phân tích phần nhạc (bài − giọng), lưu riêng với kết quả của bài gốc.
    final cacheName = vocals == null ? name : '$name#phan-nhac';
    var result = await cache.lookup(name: cacheName, size: size);
    if (result == null) {
      final analyzer = _analyzer = SongAnalyzer.create();
      final started = _clock.elapsed;
      try {
        result = await analyzer.analyze(
          path,
          vocals: vocals ?? const [],
          onProgress: (s) {
            if (mounted) setState(() => _analysisStatus = s);
          },
        );
        _analysisTime = _clock.elapsed - started;
        await cache.store(name: cacheName, size: size, analysis: result);
      } on StateError {
        return; // đã chọn beat khác: analyzer này bị dispose giữa chừng
      } on ToneCoreException catch (err) {
        if (err.code != TcResult.cancelled) {
          _analysisError = 'Không phân tích được bài (${err.codeName}).';
        }
      } on TimeoutException {
        _analysisError = 'Phân tích bài quá lâu.';
      } finally {
        if (identical(_analyzer, analyzer)) {
          analyzer.dispose();
          _analyzer = null;
        }
      }
    }
    if (!mounted) return;
    if (result != null) {
      _service.applySongAnalysis(result);
      // Chưa có preset nhớ / theo tên, người dùng chưa bấm chip, beat chưa đổi → gợi ý theo nhịp.
      if (_genreSource == null && _beatFile == name && _beatSize == size) {
        _applySuggestion(suggestGenre(fileName: name, bpm: result.bpm));
      }
    }
    setState(() {
      _analyzing = false;
      _currentKey = _service.currentKey;
    });
  }

  /// Nạp bài; có stem giọng đã tách thì nạp kèm (hỏng thì xoá stem, nạp bài thường). Trả các đoạn
  /// giọng đã dùng (null = không có).
  Future<List<String>?> _loadSong(
    ToneCoreEngine e,
    String path,
    String name,
    int size,
  ) async {
    final vocals = await _stemStore.vocalsFor(name: name, size: size);
    if (vocals != null) {
      try {
        await e.loadBeatWithVocalsAndWait(path, vocals);
        return vocals;
      } on ToneCoreException {
        await _stemStore.delete(name: name, size: size);
      }
    }
    await e.loadBeatAndWait(path);
    return null;
  }

  Future<void> _separate() async {
    final e = _service.engine;
    final path = _beatPath;
    final name = _beatFile;
    final size = _beatSize;
    final server = _server;
    if (e == null ||
        path == null ||
        name == null ||
        size == null ||
        server == null) {
      return;
    }
    if (!await _settings.separationConsent()) {
      if (!mounted) return;
      final remember = await askSeparationConsent(context);
      if (remember == null) return; // không đồng ý → không gửi
      if (remember) await _settings.setSeparationConsent(true);
    }
    final cancel = _sepCancel = SeparationCancel();
    setState(() {
      _sepError = null;
      _sepResult = null;
      _sepProgress = const SeparationProgress(
        stage: SeparationStage.preparing,
        fraction: 0,
        chunks: 0,
        uploaded: 0,
        downloaded: 0,
        elapsed: Duration.zero,
      );
    });
    final tmp = await Directory.systemTemp.createTemp('tach');
    final source = NativeChunkSource(path, tmp);
    final client = SeparationClient(
      baseUrl: server,
      deviceId: await _settings.deviceId(),
    );
    try {
      final out = await _stemStore.prepare(name: name, size: size);
      final r = await client.separate(
        source,
        out,
        cancel: cancel,
        onProgress: (p) {
          if (mounted) setState(() => _sepProgress = p);
        },
      );
      await _stemStore.markDone(
        name: name,
        size: size,
        chunks: r.vocals.length,
      );
      if (!mounted || _beatFile != name || _beatSize != size) {
        return; // đã đổi bài
      }
      final vocals = [for (final f in r.vocals) f.path];
      final transpose = _service.transpose;
      await e.loadBeatWithVocalsAndWait(path, vocals);
      _vocals = vocals;
      _sepResult = r;
      _timing.reset();
      if (transpose != 0 && _service.setTranspose(transpose)) {
        _timing.requested(transpose, _clock.elapsed);
      }
      unawaited(_analyze(path, name, size, vocals));
    } on SeparationException catch (err) {
      _sepError = err.message;
      await _stemStore.delete(name: name, size: size);
    } on ToneCoreException catch (err) {
      _sepError = 'Không nạp được giọng đã tách (${err.codeName}).';
      await _stemStore.delete(name: name, size: size);
    } finally {
      source.dispose();
      client.close();
      unawaited(tmp.delete(recursive: true).then((_) {}, onError: (_) {}));
      if (mounted) {
        setState(() {
          _sepProgress = null;
          _sepCancel = null;
          _playback = e.playback;
        });
      }
    }
  }

  Future<void> _deleteStems() async {
    final e = _service.engine;
    final path = _beatPath;
    final name = _beatFile;
    final size = _beatSize;
    if (e == null || path == null || name == null || size == null) return;
    await _stemStore.delete(name: name, size: size);
    final transpose = _service.transpose;
    try {
      await e.loadBeatAndWait(path);
    } on ToneCoreException catch (err) {
      _show('Không nạp lại được bài (${err.codeName}).');
      return;
    }
    _vocals = null;
    _sepResult = null;
    _timing.reset();
    if (transpose != 0 && _service.setTranspose(transpose)) {
      _timing.requested(transpose, _clock.elapsed);
    }
    if (mounted) setState(() => _playback = e.playback);
    unawaited(_analyze(path, name, size, null));
  }

  Future<String> _saveServer(String input) async {
    if (input.trim().isEmpty) {
      await _settings.setSeparationServer(null);
      if (mounted) setState(() => _server = null);
      return 'Đã xoá địa chỉ.';
    }
    final url = normalizeServerUrl(input);
    if (url == null) return 'Địa chỉ không hợp lệ.';
    final c = SeparationClient(
      baseUrl: url,
      deviceId: await _settings.deviceId(),
      retries: 1,
    );
    try {
      final info = await c.health();
      await _settings.setSeparationServer(url);
      if (mounted) setState(() => _server = url);
      return 'Đã kết nối (bộ tách ${info.separator}).';
    } on SeparationException catch (err) {
      return err.message;
    } finally {
      c.close();
    }
  }

  /// null → giữ preset đang dùng (người dùng chọn lần trước). Cùng preset đang dùng thì không áp
  /// lại: mức chỉnh giọng người dùng đã bấm vẫn giữ.
  void _applySuggestion(GenreSuggestion? s) {
    _genreSource = s?.source;
    if (s != null && s.genre != _service.genre) _service.setGenre(s.genre);
    if (mounted) setState(() {});
  }

  void _pickGenre(Genre g) {
    _service.setGenre(g);
    final name = _beatFile;
    final size = _beatSize;
    setState(() => _genreSource = name != null ? GenreSource.saved : null);
    unawaited(_prefs.setLastGenre(g));
    if (name != null && size != null) {
      unawaited(_prefs.setGenre(name: name, size: size, genre: g));
    }
  }

  @override
  void dispose() {
    _ticker?.cancel();
    _pitchTicker?.cancel();
    _sub?.cancel();
    _analyzer?.dispose();
    _sepCancel?.cancel();
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
          const SizedBox(height: 8),
          TransposeControl(
            value: p.transpose,
            audible: p.audibleTranspose,
            progress: p.transposeProgress,
            onChanged: _setTranspose,
          ),
        ],
        if (pb case final p? when p.hasBeat) ...[
          const SizedBox(height: 8),
          SeparationPanel(
            hasServer: _server != null,
            hasStems: _vocals != null,
            progress: _sepProgress,
            error: _sepError,
            lastResult: _sepResult,
            vocalGain: _service.vocalGain,
            onSeparate: _separate,
            onCancel: () => _sepCancel?.cancel(),
            onDelete: _deleteStems,
            onVocalGain: (v) => setState(() => _service.setVocalGain(v)),
          ),
          const SizedBox(height: 8),
        ],
        GainSlider(
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
        GainSlider(
          label: 'Âm lượng giọng',
          value: _monitorGain,
          max: 2,
          onChanged: (v) {
            setState(() => _monitorGain = v);
            e.monitorGain = v;
          },
        ),
        const Divider(height: 32),
        // ---- Hiệu ứng giọng ----
        FxPanel(
          genre: _service.genre,
          preset: _service.presetOf(_service.genre),
          source: _genreSource,
          bpm: _service.songAnalysis?.bpm,
          mix: _service.fxMix,
          gateOn: _service.gateOn,
          monitorOn: _monitorOn,
          onGenre: _pickGenre,
          onMix: (v) => setState(() => _service.setFxMix(v)),
          onGate: (v) => setState(() => _service.setGate(v)),
        ),
        const Divider(height: 32),
        // ---- Tự chỉnh giọng ----
        Text('Tự chỉnh giọng', style: theme.textTheme.titleLarge),
        const SizedBox(height: 8),
        if (_analyzing) _AnalysisProgress(status: _analysisStatus),
        if (_analysisError != null)
          Text(
            _analysisError!,
            style: TextStyle(color: theme.colorScheme.error),
          ),
        if (!_analyzing && _service.songAnalysis != null)
          _AnalysisCard(
            analysis: _service.songAnalysis!,
            follow: _service.followSong,
            onFollowChanged: (v) => setState(() {
              _service.setFollowSong(v);
              _currentKey = _service.currentKey;
            }),
          ),
        const SizedBox(height: 8),
        _KeyPicker(
          value: _currentKey ?? _service.currentKey,
          transpose: pb?.audibleTranspose ?? 0,
          followingSong: _service.followSong,
          onChanged: (k) => setState(() {
            _service.setKey(k);
            _currentKey = k;
          }),
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
        // ---- Máy chủ tách giọng (bản phát triển) ----
        ServerSettingsCard(initial: _server, onSave: _saveServer),
        const SizedBox(height: 8),
        // ---- Số đo ----
        if (m != null)
          _MetricsCard(
            metrics: m,
            timing: _timing,
            watch: playbackWatchLabel(_watch, _clock.elapsed),
            analysisTime: _analysisTime,
            rssMb: _rssMb,
            maxRssMb: _maxRssMb,
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
