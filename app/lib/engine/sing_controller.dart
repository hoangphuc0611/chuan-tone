import 'dart:async';
import 'dart:io';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/analysis_cache.dart';
import '../data/app_settings.dart';
import '../data/latency_history.dart';
import '../data/library_db.dart';
import '../data/lrc.dart';
import '../data/song_library.dart';
import '../data/song_prefs.dart';
import '../data/song_ref.dart';
import '../data/stem_store.dart';
import '../data/take_store.dart';
import 'audio_service.dart';
import 'genre_rules.dart';
import 'playback_watch.dart';
import 'separation_client.dart';
import 'song_jobs.dart';
import 'takes.dart';
import 'transpose_timing.dart';
import '../widgets/error_text.dart' show errorDetails;

/// Camera trước khi thu (iOS, AVFoundation native qua plugin chuantone_media). Test thay bằng bản
/// giả.
class SessionCamera {
  const SessionCamera();

  bool get supported => Platform.isIOS;
  Future<bool> start() async => await ChuanToneMedia.cameraStart() == true;
  Future<void> stop() => ChuanToneMedia.cameraStop();
  Future<void> recordStart(String path) =>
      ChuanToneMedia.cameraRecordStart(path);
  Future<CameraClip?> recordStop() => ChuanToneMedia.cameraRecordStop();
}

/// Tone đang dùng cho chỉnh giọng lấy từ đâu.
enum KeySource {
  /// Theo kết quả dò (tự đổi khi bài chuyển tone).
  song,

  /// Tone của bài người dùng đã sửa tay (nhớ theo bài).
  manual,

  /// Chọn tay trong phiên (Phòng thử), hoặc bài chưa dò được tone.
  picked,
}

/// Bộ điều khiển phiên hát (M8 B2, docs/m8-plan.md điểm 6): toàn bộ logic một lần hát — nạp bài
/// (kèm giọng ca sĩ đã tách), nhớ tone nhạc / thể loại theo bài, dò tone rồi cho chỉnh giọng theo
/// bài, tách giọng, nghe lại giọng, âm lượng, thu âm (+ camera), số đo. Màn hát và Phòng thử dùng
/// chung; giao diện chỉ đọc trạng thái và gọi lệnh.
///
/// Trạng thái đổi → [notifyListeners] (≈ 4 lần/giây khi đang chạy). Đồng hồ cao độ cập nhật ~30
/// lần/giây qua [pitch] riêng để không dựng lại cả màn.
class SingController extends ChangeNotifier {
  SingController({
    AudioService? service,
    Directory? baseDir,
    this.camera = const SessionCamera(),
    this.autoTick = true,
    this.countdownStep = const Duration(seconds: 1),
  }) : service = service ?? AudioService.instance,
       _baseDir = baseDir,
       _prefs = SongPrefs(baseDir: baseDir),
       settings = AppSettings(baseDir: baseDir),
       _stems = StemStore(baseDir: baseDir),
       library = SongLibrary(baseDir: baseDir),
       _takes = TakeSession(
         store: TakeStore(baseDir: baseDir),
         latency: LatencyHistory(baseDir: baseDir),
       );

  final AudioService service;
  final SessionCamera camera;
  final SongLibrary library;

  /// Cài đặt chung (máy chủ tách giọng, mẹo lần đầu…) trong cùng thư mục dữ liệu.
  final AppSettings settings;

  /// false: không tự chạy đồng hồ cập nhật (test gọi [refresh] / [pitchTick]).
  final bool autoTick;

  /// Mỗi nhịp đếm ngược trước khi thu.
  final Duration countdownStep;

  final Directory? _baseDir;
  final SongPrefs _prefs;
  final StemStore _stems;
  final TakeSession _takes;
  AnalysisCache? _cache;

  /// Màn Nghe lại & chỉnh cho bản thu vừa lưu; xong (quay về) thì nạp lại bài để hát tiếp.
  Future<void> Function(Take take)? onTake;

  /// Cao độ đang hát (~30 lần/giây).
  final pitch = ValueNotifier<PitchState?>(null);

  /// Vị trí phát (~30 lần/giây) — lời chạy chữ.
  final position = ValueNotifier<Duration>(Duration.zero);

  Timer? _ticker;
  Timer? _pitchTicker;
  StreamSubscription<ToneCoreEvent>? _sub;
  bool _disposed = false;

  bool _starting = true;
  bool _loading = false;
  SongRef? _song;
  String? _title;
  String? _message;
  String? _messageDetails;
  int _messageId = 0;
  PlaybackState? _playback;
  EngineMetrics? _metrics;
  MusicKey? _currentKey;
  double _beatGain = 0.8;
  double _monitorGain = 1.0;
  bool _monitorOn = false;

  // Phân tích bài.
  SongAnalyzer? _analyzer;
  Future<void>? _analysisFuture;
  bool _analyzing = false;
  AnalysisStatus? _analysisStatus;
  String? _analysisError;
  Duration? _analysisTime;
  MusicKey? _manualKey;

  // Lời bài hát (.lrc) của bài đang nạp.
  Lyrics? _lyrics;
  int _lyricsOffsetMs = 0;

  // Thể loại đang chọn có từ đâu (null: giữ thể loại chọn lần trước).
  GenreSource? _genreSource;

  // Tách giọng ca sĩ (M6).
  String? _server;
  List<String>? _vocals; // các đoạn giọng của bài đang nạp (null = chưa tách)
  SeparationProgress? _sepProgress;
  SeparationCancel? _sepCancel;
  String? _sepError;
  String? _sepErrorDetails;
  SeparationResult? _sepResult;

  // Thu âm (M7).
  bool _recording = false;
  int? _countdown;
  RecordStatus? _recStatus;
  bool _camera = false;
  bool _cameraRecording = false;

  // Số đo trên máy thật (M4 B7): bấm tone → nghe, render cả bài, thời gian phân tích, RAM.
  final _clock = Stopwatch()..start();
  final timing = TransposeTiming();
  final watch = PlaybackWatch();
  int _rssMb = 0;
  int _maxRssMb = 0;

  // ---- trạng thái cho giao diện ----

  /// Đang mở thiết bị âm thanh.
  bool get starting => _starting;
  ToneCoreEngine? get engine => service.engine;

  /// Lỗi mở thiết bị âm thanh / quyền micro (khi [engine] = null sau [start]).
  String? get startError => service.lastError;
  bool get loading => _loading;

  /// Bài đang nạp (null = chưa có).
  SongRef? get song => _song;

  /// Tên hiển thị của bài (tên trong thư viện).
  String? get title => _title;

  /// Thông báo cho người dùng (lỗi, cảnh báo): câu thường kèm việc cần làm; [messageDetails] =
  /// chữ kỹ thuật cho nút "Chi tiết". [messageId] tăng mỗi thông báo mới.
  String? get message => _message;
  String? get messageDetails => _messageDetails;
  int get messageId => _messageId;
  PlaybackState? get playback => _playback;
  EngineMetrics? get metrics => _metrics;

  /// Tone đang nghe (đã cộng số nửa cung đổi tone nhạc).
  MusicKey get currentKey => _currentKey ?? service.currentKey;
  KeySource get keySource => service.followSong
      ? KeySource.song
      : _manualKey != null
      ? KeySource.manual
      : KeySource.picked;
  double get beatGain => _beatGain;
  double get monitorGain => _monitorGain;
  bool get monitorOn => _monitorOn;
  bool get analyzing => _analyzing;
  AnalysisStatus? get analysisStatus => _analysisStatus;
  String? get analysisError => _analysisError;
  Duration? get analysisTime => _analysisTime;

  /// Tone của bài gốc người dùng đã sửa tay (null = theo máy dò).
  MusicKey? get manualKey => _manualKey;
  SongAnalysis? get analysis => service.songAnalysis;

  /// Lời của bài (null = chưa nạp).
  Lyrics? get lyrics => _lyrics;

  /// Lệch lời (ms, dương = lời hiện sớm hơn).
  int get lyricsOffsetMs => _lyricsOffsetMs;
  GenreSource? get genreSource => _genreSource;
  String? get server => _server;
  bool get hasStems => _vocals != null;
  SeparationProgress? get sepProgress => _sepProgress;
  String? get sepError => _sepError;
  String? get sepErrorDetails => _sepErrorDetails;
  SeparationResult? get sepResult => _sepResult;
  bool get recording => _recording;

  /// 3…1 khi đang đếm ngược trước khi thu.
  int? get countdown => _countdown;
  RecordStatus? get recStatus => _recStatus;

  /// Đang xem trước camera (sẽ quay cùng lúc thu).
  bool get cameraOn => _camera;
  Duration get clock => _clock.elapsed;
  int get rssMb => _rssMb;
  int get maxRssMb => _maxRssMb;

  /// Bấm Thu được không (có bài, không đang nạp / tách giọng / đếm ngược).
  bool get canRecord =>
      _song != null &&
      !_loading &&
      _sepProgress == null &&
      _countdown == null &&
      engine != null;

  /// Phân tích của bài đang nạp xong (test).
  @visibleForTesting
  Future<void> get analysisDone => _analysisFuture ?? Future.value();

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  void _show(String text, {Object? error}) {
    _message = text;
    _messageDetails = error == null ? null : errorDetails(error);
    _messageId++;
    _notify();
  }

  void clearMessage() {
    _message = null;
    _messageDetails = null;
    _notify();
  }

  // ---- mở phiên ----

  /// Mở thiết bị âm thanh (xin quyền micro) rồi nạp [initial] nếu có. false = không mở được
  /// ([startError]).
  Future<bool> start({SongRef? initial}) async {
    final s = service;
    if (!s.isRunning) {
      // Mở app: dùng lại thể loại chọn gần nhất (kèm mức chỉnh giọng của nó).
      final last = await _prefs.lastGenre();
      if (last != null) s.setGenre(last);
    }
    _server = await settings.separationServer();
    final ok = await s.ensureStarted();
    if (_disposed) return false;
    _starting = false;
    if (!ok) {
      _notify();
      return false;
    }
    final e = s.engine!;
    e.beatGain = _beatGain;
    _monitorOn = e.monitorEnabled;
    _monitorGain = e.monitorGain;
    _sub = s.events.listen(_onEvent);
    if (autoTick) {
      _ticker = Timer.periodic(
        const Duration(milliseconds: 250),
        (_) => refresh(),
      );
      // Đồng hồ cao độ cần mượt hơn: ~30 lần/giây.
      _pitchTicker = Timer.periodic(
        const Duration(milliseconds: 33),
        (_) => pitchTick(),
      );
    }
    _notify();
    if (initial != null) await openSong(initial);
    return true;
  }

  /// Nhịp cập nhật chậm (250 ms): vị trí phát, số đo, tone đang nghe; engine tự dừng thu khi đủ
  /// thời lượng tối đa → lưu bản thu.
  void refresh() {
    final e = engine;
    if (e == null || _disposed) return;
    if (_recording) {
      final r = _recStatus = e.recordStatus;
      if (r.state == RecordState.done || r.state == RecordState.failed) {
        unawaited(stopRecording());
      }
    }
    _playback = e.playback;
    position.value = _playback!.position;
    _metrics = e.metrics;
    _monitorOn = e.monitorEnabled; // tự bật / tắt theo tai nghe (AudioService)
    _currentKey = service.currentKey;
    _rssMb = ProcessInfo.currentRss >> 20;
    _maxRssMb = ProcessInfo.maxRss >> 20;
    _notify();
  }

  /// Nhịp nhanh (33 ms): đồng hồ cao độ, vị trí phát cho lời; đo thời gian đổi tone khi vừa bấm
  /// tone ±.
  void pitchTick() {
    final e = engine;
    if (e == null || _disposed) return;
    pitch.value = e.pitchState;
    final pb = e.playback;
    position.value = pb.position;
    final now = _clock.elapsed;
    if (timing.pending || !watch.done(now)) {
      watch.observe(at: now, position: pb.position, playing: pb.playing);
      if (timing.observe(pb.audibleTranspose, now)) {
        _playback = pb;
        _notify();
      }
    }
  }

  void _onEvent(ToneCoreEvent ev) {
    if (_disposed) return;
    switch (ev.type) {
      case ToneCoreEventType.keyChanged:
        _currentKey = ev.key;
      case ToneCoreEventType.transposeReady:
        timing.rendered(ev.code, ev.value);
      case ToneCoreEventType.monitorBlocked:
        _show(
          'Tai nghe Bluetooth bị trễ nhiều nên đã tắt nghe lại giọng. Hãy dùng tai nghe có dây.',
        );
        return;
      case ToneCoreEventType.interrupted:
        if (ev.code == 1) {
          _show('Âm thanh bị gián đoạn (cuộc gọi…)');
          return;
        }
      case ToneCoreEventType.streamRestarted:
        if (!ev.ok) {
          _show(
            'Âm thanh vừa bị lỗi. Hãy rút ra cắm lại tai nghe, hoặc tắt hẳn app rồi mở lại.',
            error: ToneCoreException('stream', ev.code),
          );
          return;
        }
      default:
        return;
    }
    _notify();
  }

  // ---- bài ----

  /// Nạp bài (kèm giọng đã tách nếu có), đặt lại tone nhạc / thể loại đã nhớ cho bài, rồi dò tone ở
  /// nền.
  Future<void> openSong(SongRef song) async {
    final e = engine;
    if (e == null || _recording) return;
    _loading = true;
    _message = null;
    _notify();
    try {
      final vocals = await _loadSong(e, song);
      if (_disposed) return;
      _song = song;
      _title =
          (await library.find(song.name, song.size))?.title ??
          titleFromFileName(song.name);
      _vocals = vocals;
      _sepError = null;
      _sepResult = vocals == null
          ? null
          : await _stems.resultFor(name: song.name, size: song.size);
      service.setVocalGain(0); // bài mới: karaoke
      // Nạp nhạc mới → engine về tone gốc; đặt lại tone người dùng đã chọn cho bài này.
      final saved = await _prefs.transposeFor(name: song.name, size: song.size);
      timing.reset();
      _analysisTime = null;
      if (saved != 0 && service.setTranspose(saved)) {
        timing.requested(saved, _clock.elapsed);
      }
      _manualKey = await _prefs.keyFor(name: song.name, size: song.size);
      final ly = await library.lyricsFor(song.name, song.size);
      _lyrics = ly?.lyrics;
      _lyricsOffsetMs = ly?.offsetMs ?? 0;
      // Thể loại: đã chọn cho bài → theo tên file; theo nhịp thì chờ phân tích xong.
      _applySuggestion(
        suggestGenre(
          saved: await _prefs.genreFor(name: song.name, size: song.size),
          fileName: song.name,
        ),
      );
      _playback = e.playback;
      position.value = _playback!.position;
      unawaited(library.touch(song.name, song.size));
      _analyze(song, vocals);
    } on ToneCoreException catch (err) {
      _show(
        'Không mở được bài này. Hãy thử file MP3, M4A hoặc WAV khác.',
        error: err,
      );
    } on TimeoutException catch (err) {
      _show('Mở bài quá lâu. Hãy thử lại.', error: err);
    }
    _loading = false;
    _notify();
  }

  /// Nạp bài; có stem giọng đã tách thì nạp kèm (hỏng thì xoá stem, nạp bài thường). Trả các đoạn
  /// giọng đã dùng (null = không có).
  Future<List<String>?> _loadSong(ToneCoreEngine e, SongRef song) async {
    final vocals = await _stems.vocalsFor(name: song.name, size: song.size);
    if (vocals != null) {
      try {
        await e.loadBeatWithVocalsAndWait(song.path, vocals);
        return vocals;
      } on ToneCoreException {
        await _stems.delete(name: song.name, size: song.size);
      }
    }
    await e.loadBeatAndWait(song.path);
    return null;
  }

  void _analyze(SongRef song, List<String>? vocals) {
    _analysisFuture = _runAnalysis(song, vocals);
  }

  /// Dò tone / chuyển tone / tuning / BPM của bài vừa nạp (lấy từ bộ nhớ nếu đã phân tích; đã tách
  /// giọng thì phân tích phần nhạc), rồi cho chỉnh giọng theo bài — trừ khi người dùng đã sửa tone
  /// tay cho bài.
  Future<void> _runAnalysis(SongRef song, List<String>? vocals) async {
    _analyzer?.dispose(); // bài trước còn đang phân tích → dừng
    _analyzer = null;
    service.applySongAnalysis(null);
    _analyzing = true;
    _analysisStatus = null;
    _analysisError = null;
    _notify();
    final cache = _cache ??= AnalysisCache(
      engineVersion: ToneCore.version,
      baseDir: _baseDir,
    );
    SongAnalysis? result;
    try {
      final (r, took) = await analyzeSong(
        song,
        vocals: vocals,
        cache: cache,
        onAnalyzer: (a) => _analyzer = a,
        onProgress: (s) {
          _analysisStatus = s;
          _notify();
        },
      );
      result = r;
      if (took != null) _analysisTime = took;
    } on StateError {
      return; // đã chọn bài khác: analyzer này bị dispose giữa chừng
    } on ToneCoreException catch (err) {
      if (err.code != TcResult.cancelled) {
        _analysisError = 'Chưa dò được tone của bài. Bạn vẫn hát được; sửa tone tay ở ⋮ → Sửa tone của bài.';
      }
    } on TimeoutException {
      _analysisError = 'Dò tone quá lâu. Bạn vẫn hát được; sửa tone tay ở ⋮ → Sửa tone của bài.';
    } finally {
      _analyzer = null;
    }
    if (_disposed || !_isCurrent(song)) return;
    if (result != null) {
      service.applySongAnalysis(result);
      final manual = _manualKey;
      if (manual != null) service.setBaseKey(manual);
      // Chưa có thể loại nhớ / theo tên, người dùng chưa bấm chip → gợi ý theo nhịp.
      if (_genreSource == null) {
        _applySuggestion(suggestGenre(fileName: song.name, bpm: result.bpm));
      }
    }
    _analyzing = false;
    _currentKey = service.currentKey;
    _notify();
  }

  bool _isCurrent(SongRef song) =>
      _song?.name == song.name && _song?.size == song.size;

  // ---- tone ----

  /// Đổi tone nhạc (−6…+6), nhớ theo bài. Đang thu thì khoá.
  void setTranspose(int semitones) {
    final e = engine;
    if (e == null || _recording || !service.setTranspose(semitones)) return;
    timing.requested(service.transpose, _clock.elapsed);
    watch.restart(_clock.elapsed);
    _playback = e.playback;
    _notify();
    final s = _song;
    if (s != null) {
      unawaited(
        _prefs.setTranspose(
          name: s.name,
          size: s.size,
          semitones: service.transpose,
        ),
      );
    }
  }

  /// Sửa tay tone của bài gốc (nhớ theo bài, như màn Phân tích bài); null = theo máy dò.
  Future<void> setManualKey(MusicKey? key) async {
    final s = _song;
    if (s == null) return;
    await _prefs.setKey(name: s.name, size: s.size, key: key);
    _manualKey = key;
    if (key != null) {
      service.setBaseKey(key);
    } else {
      service.setFollowSong(true);
    }
    _currentKey = service.currentKey;
    _notify();
  }

  /// Chọn tay tone đang nghe trong phiên (Phòng thử) → thôi theo bài.
  void setKey(MusicKey heard) {
    service.setKey(heard);
    _currentKey = heard;
    _notify();
  }

  /// Tone theo bài (tự đổi khi bài chuyển tone) / tone chọn tay.
  void setFollowSong(bool on) {
    service.setFollowSong(on);
    _currentKey = service.currentKey;
    _notify();
  }

  // ---- giọng ----

  /// Mức chỉnh giọng (null = tắt); giữ tới khi chọn thể loại khác.
  void setCorrection(CorrectionPreset? p) {
    service.setCorrection(p);
    _notify();
  }

  /// null → giữ thể loại đang dùng (người dùng chọn lần trước). Cùng thể loại đang dùng thì không
  /// áp lại: mức chỉnh giọng người dùng đã bấm vẫn giữ.
  void _applySuggestion(GenreSuggestion? s) {
    _genreSource = s?.source;
    if (s != null && s.genre != service.genre) service.setGenre(s.genre);
    _notify();
  }

  /// Người dùng bấm chip thể loại: áp ngay (đổi khi đang hát không click), nhớ theo bài và làm
  /// thể loại mặc định lần mở app sau.
  void pickGenre(Genre g) {
    service.setGenre(g);
    final s = _song;
    _genreSource = s != null ? GenreSource.saved : null;
    _notify();
    unawaited(_prefs.setLastGenre(g));
    if (s != null) {
      unawaited(_prefs.setGenre(name: s.name, size: s.size, genre: g));
    }
  }

  void setFxMix(double v) {
    service.setFxMix(v);
    _notify();
  }

  void setGate(bool on) {
    service.setGate(on);
    _notify();
  }

  // ---- âm lượng ----

  void setBeatGain(double v) {
    _beatGain = v.clamp(0.0, 1.0);
    engine?.beatGain = _beatGain;
    _notify();
  }

  void setMonitorGain(double v) {
    _monitorGain = v.clamp(0.0, 2.0);
    engine?.monitorGain = _monitorGain;
    _notify();
  }

  void setMonitorOn(bool on) {
    _monitorOn = on;
    engine?.monitorEnabled = on;
    _notify();
  }

  /// "Giữ giọng ca sĩ gốc" 0..1 (bài đã tách giọng).
  void setVocalGain(double v) {
    service.setVocalGain(v);
    _notify();
  }

  // ---- phát ----

  void playPause() {
    final e = engine;
    if (e == null || !e.playback.hasBeat) return;
    e.playback.playing ? e.pause() : e.play();
    _playback = e.playback;
    _notify();
  }

  void seek(Duration to) {
    final e = engine;
    if (e == null) return;
    e.seek(to < Duration.zero ? Duration.zero : to);
    _playback = e.playback;
    position.value = _playback!.position;
    _notify();
  }

  /// Tua lùi / tới [delta] tính từ vị trí đang phát.
  void skip(Duration delta) {
    final p = engine?.playback;
    if (p == null || !p.hasBeat) return;
    final to = p.position + delta;
    seek(to > p.duration ? p.duration : to);
  }

  // ---- lời bài hát ----

  /// Nạp lời `.lrc` cho bài đang hát (nhớ theo bài). File không phải lời có mốc thời gian →
  /// [FormatException].
  Future<void> setLyricsFile(String path) async {
    final s = _song;
    if (s == null) return;
    _lyrics = await library.setLyrics(s.name, s.size, path);
    _lyricsOffsetMs = 0;
    _notify();
  }

  Future<void> removeLyrics() async {
    final s = _song;
    if (s == null) return;
    await library.removeLyrics(s.name, s.size);
    _lyrics = null;
    _lyricsOffsetMs = 0;
    _notify();
  }

  /// Dời lời [deltaMs] (dương = sớm hơn), trong ±10 s, nhớ theo bài.
  void nudgeLyrics(int deltaMs) {
    final s = _song;
    if (s == null || _lyrics == null) return;
    _lyricsOffsetMs = (_lyricsOffsetMs + deltaMs).clamp(-10000, 10000);
    _notify();
    unawaited(library.setLyricsOffset(s.name, s.size, _lyricsOffsetMs));
  }

  // ---- thu âm ----

  /// Thu âm: nhạc đang dừng thì đếm ngược 3 giây rồi phát từ vị trí hiện tại. Khoá tone khi thu.
  Future<void> startRecording() async {
    final e = engine;
    if (e == null || !canRecord || _recording) return;
    final wasPlaying = e.playback.playing;
    if (!wasPlaying) {
      for (var n = 3; n > 0; n--) {
        _countdown = n;
        _notify();
        await Future<void>.delayed(countdownStep);
        if (_disposed) return;
      }
      _countdown = null;
    }
    try {
      final dir = await _takes.start(
        e,
        TakeSnapshot.of(
          service,
          e,
          voiceGain: _monitorGain,
          beatGain: _beatGain,
        ),
      );
      if (_camera) {
        try {
          await camera.recordStart('${dir.path}/camera.mp4');
          _cameraRecording = true;
        } on PlatformException catch (err) {
          _show('Không quay được video, vẫn thu âm bình thường.', error: err);
        }
      }
    } on ToneCoreException catch (err) {
      _show('Chưa thu được. Hãy dừng nhạc rồi bấm Thu lại.', error: err);
      return;
    }
    if (!wasPlaying) e.play();
    _recording = true;
    _recStatus = e.recordStatus;
    _playback = e.playback;
    _notify();
  }

  /// Dừng thu → lưu bản thu → [onTake] (màn Nghe lại & chỉnh); quay về thì nạp lại bài. Trả bản thu
  /// (null nếu không lưu được).
  Future<Take?> stopRecording() async {
    final e = engine;
    final song = _song;
    if (e == null || song == null || !_recording) return null;
    _recording = false;
    _notify();
    e.pause();
    CameraClip? clip;
    if (_cameraRecording) {
      _cameraRecording = false;
      try {
        clip = await camera.recordStop();
      } on PlatformException catch (err) {
        _show('Video camera bị lỗi; phần thu âm vẫn được lưu.', error: err);
      }
    }
    final Take take;
    try {
      take = await _takes.stop(e, song, camera: clip);
    } on ToneCoreException catch (err) {
      _show(
        'Không lưu được bản thu. Máy có thể đã đầy — xoá bớt ở Cài đặt → Bộ nhớ.',
        error: err,
      );
      return null;
    }
    final review = onTake;
    if (review == null || _disposed) return take;
    await review(take);
    if (_disposed) return take;
    // Màn Nghe lại nạp bản thu thay nhạc → nạp lại bài, trả âm lượng của phiên.
    e.beatGain = _beatGain;
    e.monitorGain = _monitorGain;
    await openSong(song);
    return take;
  }

  /// Bật / tắt xem trước camera trước (quay cùng lúc thu).
  Future<void> setCamera(bool on) async {
    if (!on) {
      _camera = false;
      _notify();
      await camera.stop();
      return;
    }
    try {
      if (await camera.start() && !_disposed) {
        _camera = true;
        _notify();
      }
    } on PlatformException catch (err) {
      _show(
        'Không mở được camera. Hãy cho ChuẩnTone dùng camera trong Cài đặt của máy.',
        error: err,
      );
    }
  }

  // ---- tách giọng ca sĩ ----

  /// Tách giọng ca sĩ của bài đang nạp qua máy chủ. Chưa đồng ý gửi bài thì hỏi [askConsent]
  /// (null = không đồng ý → không gửi; true = nhớ lựa chọn).
  Future<void> separate({required Future<bool?> Function() askConsent}) async {
    final e = engine;
    final song = _song;
    final server = _server;
    if (e == null || song == null || server == null || _recording) return;
    if (!await settings.separationConsent()) {
      final remember = await askConsent();
      if (remember == null || _disposed) return;
      if (remember) await settings.setSeparationConsent(true);
    }
    final cancel = _sepCancel = SeparationCancel();
    _sepError = null;
    _sepErrorDetails = null;
    _sepResult = null;
    _sepProgress = SeparationProgress.start;
    _notify();
    try {
      final r = await separateSong(
        song,
        server: server,
        settings: settings,
        store: _stems,
        cancel: cancel,
        onProgress: (p) {
          _sepProgress = p;
          _notify();
        },
      );
      if (_disposed || !_isCurrent(song)) return; // đã đổi bài
      final vocals = [for (final f in r.vocals) f.path];
      final transpose = service.transpose;
      await e.loadBeatWithVocalsAndWait(song.path, vocals);
      _vocals = vocals;
      _sepResult = r;
      timing.reset();
      if (transpose != 0 && service.setTranspose(transpose)) {
        timing.requested(transpose, _clock.elapsed);
      }
      _analyze(song, vocals);
    } on SeparationException catch (err) {
      _sepError = err.message;
      _sepErrorDetails = err.details;
    } on ToneCoreException {
      _sepError = 'Không dùng được giọng đã tách. Hãy tách lại.';
      await _stems.delete(name: song.name, size: song.size);
    } finally {
      _sepProgress = null;
      _sepCancel = null;
      _playback = e.playback;
      _notify();
    }
  }

  void cancelSeparation() => _sepCancel?.cancel();

  /// Xoá bản tách giọng của bài đang nạp, nạp lại bài thường.
  Future<void> deleteStems() async {
    final e = engine;
    final song = _song;
    if (e == null || song == null || _recording) return;
    await _stems.delete(name: song.name, size: song.size);
    final transpose = service.transpose;
    try {
      await e.loadBeatAndWait(song.path);
    } on ToneCoreException catch (err) {
      _show('Không mở lại được bài. Hãy mở bài từ Trang chủ.', error: err);
      return;
    }
    _vocals = null;
    _sepResult = null;
    service.setVocalGain(0);
    timing.reset();
    if (transpose != 0 && service.setTranspose(transpose)) {
      timing.requested(transpose, _clock.elapsed);
    }
    _playback = e.playback;
    _notify();
    _analyze(song, null);
  }

  /// Lưu địa chỉ máy chủ tách giọng (kiểm tra kết nối trước); rỗng = xoá. Trả câu báo kết quả.
  Future<String> saveServer(String input) async {
    final r = await saveSeparationServer(settings, input);
    if (r.changed) {
      _server = r.url;
      _notify();
    }
    return r.message;
  }

  /// Gọi nhiều lần cũng được (màn hình và người tạo cùng huỷ).
  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _ticker?.cancel();
    _pitchTicker?.cancel();
    _sub?.cancel();
    _analyzer?.dispose();
    _sepCancel?.cancel();
    if (_recording) {
      try {
        // Rời màn khi đang thu: vẫn giữ file đã ghi.
        engine?.stopRecording();
      } on ToneCoreException {
        // bỏ qua
      }
    }
    if (_cameraRecording) {
      unawaited(camera.recordStop().then((_) {}, onError: (_) {}));
    }
    if (_camera) unawaited(camera.stop().then((_) {}, onError: (_) {}));
    engine?.pause();
    pitch.dispose();
    position.dispose();
    super.dispose();
  }
}
