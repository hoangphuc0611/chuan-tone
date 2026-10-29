import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'genre_rules.dart';
import 'labels.dart';

/// Giữ một engine ToneCore dùng chung cho cả app (thiết bị âm thanh thật).
///
/// Mở engine khi màn đầu tiên cần, xin quyền micro trước khi mở.
class AudioService extends ChangeNotifier {
  AudioService._();

  static final AudioService instance = AudioService._();

  ToneCoreEngine? _engine;
  RouteMonitor? _routeMonitor;
  StreamSubscription<ToneCoreEvent>? _eventSub;
  final _events = StreamController<ToneCoreEvent>.broadcast();

  String? lastError;
  bool micGranted = false;

  /// Tone chọn tay cho tự chỉnh giọng, tính theo beat GỐC (mặc định La thứ – tone phổ biến của
  /// nhạc Việt). Khi beat đã đổi tone, engine tự cộng số nửa cung.
  MusicKey key = const MusicKey(9, MusicScale.naturalMinor);

  /// Kết quả phân tích của beat đang nạp (null = chưa có).
  SongAnalysis? songAnalysis;

  /// true: tone (và chuyển tone) theo kết quả phân tích, engine tự đổi theo vị trí bài;
  /// false: dùng [key] chọn tay.
  bool followSong = false;

  /// Mức tự chỉnh giọng; null = tắt.
  CorrectionPreset? correction;

  /// Thời gian kéo về nốt do preset thể loại đặt (thay cho giá trị của [correction]); null = theo
  /// mức. Bấm nút chỉnh giọng thì về null.
  double? correctionRetuneMs;

  /// Preset hiệu ứng giọng đang chọn.
  Genre genre = Genre.dry;

  /// "Vang": 0..2 (1 = đúng preset).
  double fxMix = 1.0;

  /// Cổng chặn tiếng ồn micro.
  bool gateOn = false;

  final _presets = <Genre, FxPreset>{};

  /// Thông số của preset thể loại (lấy từ engine một lần).
  FxPreset presetOf(Genre g) => _presets[g] ??= ToneCore.genrePreset(g);

  ToneCoreEngine? get engine => _engine;

  /// Tone đang áp dụng = tone đang nghe (khi theo bài thì đổi theo vị trí beat; đã cộng số nửa
  /// cung đổi tone beat).
  MusicKey get currentKey => _engine?.appliedKey ?? key;

  /// Tone beat đã chọn (−6…+6, 0 = gốc).
  int get transpose => _engine?.transpose ?? 0;
  bool get isRunning => _engine != null;

  /// Sự kiện từ engine (nạp beat xong, đổi route, đo độ trễ xong...).
  Stream<ToneCoreEvent> get events => _events.stream;

  /// Xin quyền micro và mở thiết bị âm thanh. Trả về true nếu chạy được.
  Future<bool> ensureStarted() async {
    if (_engine != null) return true;
    micGranted = await MicPermission.request();
    if (!micGranted) {
      lastError = 'Chưa cấp quyền micro. Vào Cài đặt của máy để bật quyền micro cho ChuẩnTone.';
      notifyListeners();
      return false;
    }
    try {
      final e = ToneCoreEngine.platform();
      e.start();
      e.key = key;
      _engine = e;
      _syncCorrection();
      _syncFx();
      _syncSong();
      _routeMonitor = RouteMonitor(e)..start();
      _eventSub = e.events.listen(_events.add);
      lastError = null;
    } on ToneCoreException catch (err) {
      lastError = 'Không mở được thiết bị âm thanh (${err.codeName}).';
    }
    notifyListeners();
    return _engine != null;
  }

  /// Chọn tay tone đang NGHE (như hiển thị) → thôi theo bài. Beat đã đổi tone thì quy về tone
  /// beat gốc trước khi giao engine.
  void setKey(MusicKey heard) {
    key = baseKeyForHeard(heard, transpose);
    followSong = false;
    _engine?.key = key;
    notifyListeners();
  }

  /// Đổi tone beat (−6…+6). Trả false nếu chưa nạp beat.
  bool setTranspose(int semitones) {
    final e = _engine;
    if (e == null) return false;
    try {
      e.transpose = semitones.clamp(-6, 6);
    } on ToneCoreException {
      return false;
    }
    notifyListeners();
    return true;
  }

  /// Nhận kết quả phân tích của beat mới (null khi đổi beat mà chưa có kết quả): áp tuning, và
  /// theo tone của bài nếu dò được tone.
  void applySongAnalysis(SongAnalysis? a) {
    songAnalysis = a;
    followSong = a?.key != null;
    _syncSong();
    notifyListeners();
  }

  void setFollowSong(bool on) {
    followSong = on && songAnalysis?.key != null;
    _syncSong();
    notifyListeners();
  }

  void _syncSong() {
    final e = _engine;
    if (e == null) return;
    final a = songAnalysis;
    e.tuningCents = a?.tuningCents ?? 0;
    e.tempo = a?.bpm ?? 0; // delay theo nhịp bài; chưa rõ → engine dùng 100 BPM
    if (followSong && a != null && a.timeline.isNotEmpty) {
      e.setKeyTimeline(a.timeline);
    } else {
      e.key = key;
    }
  }

  /// Người dùng bấm nút chỉnh giọng (null = tắt): giữ lựa chọn này tới khi chọn preset khác.
  void setCorrection(CorrectionPreset? p) {
    correction = p;
    correctionRetuneMs = null;
    _syncCorrection();
    notifyListeners();
  }

  void _syncCorrection() {
    final e = _engine;
    if (e == null) return;
    final p = correction;
    if (p != null) {
      e.applyCorrectionPreset(p);
      final retune = correctionRetuneMs;
      if (retune != null) e.setParam(TcNode.pitch, TcParam.retuneMs, retune);
    }
    e.pitchCorrectionEnabled = p != null;
  }

  /// Chọn preset hiệu ứng giọng (đổi khi đang hát không click) và đặt luôn mức tự chỉnh giọng theo
  /// thể loại ([genreCorrection]; Khô giữ nguyên).
  void setGenre(Genre g) {
    genre = g;
    final c = genreCorrection(g);
    if (!c.keep) {
      correction = c.level;
      correctionRetuneMs = c.retuneMs;
      _syncCorrection();
    }
    _engine?.applyFxPreset(presetOf(g));
    notifyListeners();
  }

  void setFxMix(double v) {
    fxMix = v.clamp(0.0, 2.0);
    _engine?.fxMix = fxMix;
    notifyListeners();
  }

  void setGate(bool on) {
    gateOn = on;
    _engine?.gateEnabled = on;
    notifyListeners();
  }

  void _syncFx() {
    final e = _engine;
    if (e == null) return;
    e.applyFxPreset(presetOf(genre));
    e.fxMix = fxMix;
    e.gateEnabled = gateOn;
  }

  Future<void> shutdown() async {
    await _eventSub?.cancel();
    _eventSub = null;
    _routeMonitor?.stop();
    _routeMonitor = null;
    _engine?.dispose();
    _engine = null;
    notifyListeners();
  }
}
