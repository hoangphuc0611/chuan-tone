import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/app_settings.dart';
import '../widgets/error_text.dart' show errorDetails;
import 'genre_rules.dart';
import 'labels.dart';

/// Giữ một engine ToneCore dùng chung cho cả app (thiết bị âm thanh thật).
///
/// Mở engine khi màn đầu tiên cần, xin quyền micro trước khi mở.
class AudioService extends ChangeNotifier {
  AudioService._(this._create, [this._settings]);

  /// Dịch vụ cho test: engine do [create] tạo (vd backend NULL, xử lý bằng `processOffline`);
  /// [settings] = cài đặt micro / nghe lại giọng đọc lúc mở (null = mặc định).
  @visibleForTesting
  AudioService.test(ToneCoreEngine Function() create, {AppSettings? settings})
    : this._(create, settings);

  static AudioService get instance => _instance;
  static AudioService _instance = AudioService._(
    ToneCoreEngine.platform,
    AppSettings(),
  );

  /// Lượt chạy toàn app trong test (test/walkthrough_test.dart): mọi màn dùng dịch vụ này.
  @visibleForTesting
  static set instanceForTesting(AudioService s) => _instance = s;

  final ToneCoreEngine Function() _create;
  final AppSettings? _settings;

  /// Micro đã chọn (Cài đặt → Micro).
  InputKind inputPreference = InputKind.auto;

  /// Tự bật nghe lại giọng khi dùng tai nghe có dây / soundcard, tắt khi về loa ngoài.
  bool autoMonitor = true;

  /// Chế độ soundcard (M8 B6): null = chưa hỏi; true = cắm thiết bị USB thì coi là soundcard (tắt
  /// nghe lại giọng trong app vì soundcard đã cho nghe giọng trực tiếp, không trễ).
  bool? soundcardMode;

  /// Hiệu ứng giọng ở chế độ soundcard.
  SoundcardFx soundcardFx = SoundcardFx.soundcard;

  /// Đường tiếng hiện tại (theo sự kiện đổi route của engine).
  AudioRoute route = AudioRoute.unknown;

  bool _soundcardAsked = false;

  ToneCoreEngine? _engine;
  RouteMonitor? _routeMonitor;
  StreamSubscription<ToneCoreEvent>? _eventSub;
  final _events = StreamController<ToneCoreEvent>.broadcast();

  /// Lỗi mở âm thanh cho người dùng (câu thường kèm việc cần làm); [lastErrorDetails] = chữ kỹ
  /// thuật cho nút "Chi tiết".
  String? lastError;
  String? lastErrorDetails;
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

  /// "Giữ giọng ca sĩ gốc" 0..1 khi beat có stem giọng (0 = karaoke).
  double vocalGain = 0.0;

  final _presets = <Genre, FxPreset>{};

  /// Thông số của preset thể loại (lấy từ engine một lần).
  FxPreset presetOf(Genre g) => _presets[g] ??= ToneCore.genrePreset(g);

  ToneCoreEngine? get engine => _engine;

  /// Số lần đã nạp bài / bản thu vào engine (mỗi màn nạp thì tăng). Màn Nghe lại chỉ gỡ bản thu
  /// của mình khi chưa ai nạp gì sau nó: quay về Màn hát thì Màn hát nạp lại bài ngay lúc bấm quay
  /// về, còn màn Nghe lại chỉ đóng hẳn sau hiệu ứng chuyển màn — gỡ lúc đó là gỡ mất bài vừa nạp.
  int loads = 0;

  /// Tone đang áp dụng = tone đang nghe (khi theo bài thì đổi theo vị trí beat; đã cộng số nửa
  /// cung đổi tone beat).
  MusicKey get currentKey => _engine?.appliedKey ?? key;

  /// Tone beat đã chọn (−6…+6, 0 = gốc).
  int get transpose => _engine?.transpose ?? 0;
  bool get isRunning => _engine != null;

  /// Sự kiện từ engine (nạp beat xong, đổi route, đo độ trễ xong...).
  Stream<ToneCoreEvent> get events => _events.stream;

  /// Đang ở chế độ soundcard: đã chọn và đang cắm thiết bị USB.
  bool get soundcardActive => soundcardMode == true && route == AudioRoute.usb;

  /// Giữ vang của soundcard: bản thu để preset Khô (vang đã có sẵn trong tiếng micro).
  bool get usingSoundcardFx =>
      soundcardActive && soundcardFx == SoundcardFx.soundcard;

  /// Thể loại ghi vào bản thu (preset dùng khi Nghe lại / xuất).
  Genre get takeGenre => usingSoundcardFx ? Genre.dry : genre;

  /// Vừa cắm thiết bị USB mà chưa chọn chế độ soundcard → giao diện hỏi (một lần mỗi lần mở app;
  /// gọi [markSoundcardAsked] khi hỏi).
  bool get shouldAskSoundcard =>
      soundcardMode == null && route == AudioRoute.usb && !_soundcardAsked;

  void markSoundcardAsked() => _soundcardAsked = true;

  /// Xin quyền micro và mở thiết bị âm thanh. Trả về true nếu chạy được.
  Future<bool> ensureStarted() async {
    if (_engine != null) return true;
    micGranted = await MicPermission.request();
    if (!micGranted) {
      lastError = 'Chưa cấp quyền micro. Vào Cài đặt của máy để bật quyền micro cho ChuẩnTone.';
      notifyListeners();
      return false;
    }
    final settings = _settings;
    if (settings != null) {
      inputPreference = await settings.inputPreference();
      autoMonitor = await settings.autoMonitor();
      soundcardMode = await settings.soundcardMode();
      soundcardFx = await settings.soundcardFx();
    }
    // Android: id của micro đã chọn (tìm trước khi mở để mở đúng micro ngay).
    final deviceId = await InputDevices.idFor(inputPreference);
    if (_engine != null) return true; // màn khác đã mở trong lúc chờ
    try {
      final e = _create();
      e.inputWanted = inputPreference;
      e.setInputPreference(inputPreference, deviceId: deviceId);
      e.start();
      e.key = key;
      _engine = e;
      _syncCorrection();
      _syncFx();
      _syncSong();
      _routeMonitor = RouteMonitor(e)..start();
      _eventSub = e.events.listen(_onEngineEvent);
      _applyRoute(e.metrics.route);
      lastError = null;
      lastErrorDetails = null;
    } catch (err) {
      // Thiết bị âm thanh bận / lỗi, thiếu thư viện engine…
      lastError = 'Không mở được âm thanh. Hãy tắt hẳn app rồi mở lại; nếu vẫn lỗi, khởi động lại máy.';
      lastErrorDetails = errorDetails(err);
    }
    notifyListeners();
    return _engine != null;
  }

  void _onEngineEvent(ToneCoreEvent ev) {
    if (ev.type == ToneCoreEventType.routeChanged) {
      _applyRoute(
        AudioRoute.values[ev.code.clamp(0, AudioRoute.values.length - 1)],
      );
    }
    _events.add(ev);
  }

  /// Nhận đường tiếng mới. Chế độ soundcard: tắt nghe lại giọng — soundcard đã cho nghe giọng
  /// trực tiếp, nghe thêm qua app thành giọng đôi lệch nhau; soundcard bật Loopback còn thành vòng
  /// hú (tiếng ra → soundcard → micro → tiếng ra). Còn lại: tự bật nghe lại giọng khi cắm tai nghe có
  /// dây / USB, tắt khi về loa ngoài (tránh hú); Bluetooth engine tự chặn; không rõ: giữ nguyên.
  void _applyRoute(AudioRoute r) {
    route = r;
    final e = _engine;
    if (e == null) return;
    if (soundcardActive) {
      e.monitorEnabled = false;
    } else if (autoMonitor) {
      switch (r) {
        case AudioRoute.wired || AudioRoute.usb:
          e.monitorEnabled = true;
        case AudioRoute.speaker || AudioRoute.receiver:
          e.monitorEnabled = false;
        default:
          break;
      }
    }
    notifyListeners();
  }

  /// Chọn micro (lưu ở Cài đặt; engine đang chạy thì đổi ngay).
  Future<void> setInputPreference(InputKind kind) async {
    inputPreference = kind;
    await _settings?.setInputPreference(kind);
    await _engine?.selectInput(kind);
    notifyListeners();
  }

  /// Bật / tắt tự nghe lại giọng theo đường tiếng (bật thì áp ngay theo đường tiếng hiện tại).
  /// Lưu cài đặt do người gọi làm (Cài đặt).
  void updateAutoMonitor(bool on) {
    autoMonitor = on;
    final e = _engine;
    if (on && e != null) _applyRoute(e.metrics.route);
    notifyListeners();
  }

  /// Đổi chế độ soundcard / hiệu ứng (lưu cài đặt do người gọi làm). Áp ngay theo đường tiếng hiện
  /// tại: bật khi đang cắm soundcard thì tắt nghe lại giọng; tắt thì về cách tự bật / tắt thường.
  void updateSoundcard({bool? mode, SoundcardFx? fx}) {
    if (mode != null) soundcardMode = mode;
    if (fx != null) soundcardFx = fx;
    _soundcardAsked = true;
    _applyRoute(route);
    notifyListeners();
  }

  /// Chọn tay tone đang NGHE (như hiển thị) → thôi theo bài. Beat đã đổi tone thì quy về tone
  /// beat gốc trước khi giao engine.
  void setKey(MusicKey heard) {
    key = baseKeyForHeard(heard, transpose);
    followSong = false;
    _engine?.key = key;
    notifyListeners();
  }

  /// Tone của beat GỐC người dùng đã sửa tay cho bài (màn Phân tích bài) → thôi theo bài.
  void setBaseKey(MusicKey base) {
    key = base;
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

  /// Đặt cả chuỗi giọng theo một bản thu (màn Nghe lại, M7): thể loại, mức chỉnh giọng (kèm thời
  /// gian kéo về nốt của thể loại), "Vang", cổng — không lấy mức chỉnh giọng mặc định của thể loại.
  void applyVoiceSettings({
    required Genre genre,
    required CorrectionPreset? correction,
    required double? retuneMs,
    required double fxMix,
    required bool gateOn,
  }) {
    this.genre = genre;
    this.correction = correction;
    correctionRetuneMs = retuneMs;
    this.fxMix = fxMix.clamp(0.0, 2.0);
    this.gateOn = gateOn;
    _syncCorrection();
    _syncFx();
    notifyListeners();
  }

  void setFxMix(double v) {
    fxMix = v.clamp(0.0, 2.0);
    _engine?.fxMix = fxMix;
    notifyListeners();
  }

  void setVocalGain(double v) {
    vocalGain = v.clamp(0.0, 1.0);
    _engine?.vocalGain = vocalGain;
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
    e.vocalGain = vocalGain;
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
