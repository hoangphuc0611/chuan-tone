// Lớp Dart thân thiện bọc C API của ToneCore (xem docs/adr/001-c-api-ffi.md).

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'fx_preset.dart';
import 'tonecore_bindings_generated.dart';

/// Phiên bản major của C API mà binding này được sinh ra cho.
const int kExpectedApiMajor = TC_API_VERSION_MAJOR;

/// Mã lỗi trả về từ C API (giá trị lấy từ binding sinh tự động).
abstract final class TcResult {
  static const int ok = tc_result_code.TC_OK;
  static const int invalidArg = tc_result_code.TC_ERR_INVALID_ARG;
  static const int outOfMemory = tc_result_code.TC_ERR_OUT_OF_MEMORY;
  static const int invalidState = tc_result_code.TC_ERR_INVALID_STATE;
  static const int notSupported = tc_result_code.TC_ERR_NOT_SUPPORTED;
  static const int internal = tc_result_code.TC_ERR_INTERNAL;
  static const int device = tc_result_code.TC_ERR_DEVICE;
  static const int cancelled = tc_result_code.TC_ERR_CANCELLED;
}

abstract final class TcNode {
  static const int master = tc_node_id.TC_NODE_MASTER;
  static const int monitor = tc_node_id.TC_NODE_MONITOR;
  static const int beat = tc_node_id.TC_NODE_BEAT;
  static const int pitch = tc_node_id.TC_NODE_PITCH;
  static const int gate = tc_node_id.TC_NODE_GATE;
  static const int fx = tc_node_id.TC_NODE_FX;
}

abstract final class TcParam {
  static const int gain = tc_param_id.TC_PARAM_GAIN;
  static const int enabled = tc_param_id.TC_PARAM_ENABLED;
  static const int retuneMs = tc_param_id.TC_PARAM_RETUNE_MS;
  static const int humanize = tc_param_id.TC_PARAM_HUMANIZE;
  static const int flexCents = tc_param_id.TC_PARAM_FLEX_CENTS;
  static const int vibratoPreserve = tc_param_id.TC_PARAM_VIBRATO_PRESERVE;
  static const int hysteresisCents = tc_param_id.TC_PARAM_HYSTERESIS_CENTS;
  static const int mix = tc_param_id.TC_PARAM_MIX;
  static const int thresholdDb = tc_param_id.TC_PARAM_THRESHOLD_DB;
}

/// Thang âm (thứ tự khớp `tc_scale`).
enum MusicScale {
  major,
  naturalMinor,
  harmonicMinor,
  pentatonicMajor,
  pentatonicMinor,
  chromatic,
}

/// 3 mức "Tự chỉnh giọng" (thứ tự khớp `tc_correction_preset`).
enum CorrectionPreset { natural, singer, robot }

/// Tone (giọng) của bài: chủ âm 0 = Đô (C) … 11 = Si (B) + thang âm.
class MusicKey {
  const MusicKey(this.root, this.scale)
    : assert(root >= 0 && root < 12, 'root phải trong 0..11');

  final int root;
  final MusicScale scale;

  @override
  bool operator ==(Object other) =>
      other is MusicKey && other.root == root && other.scale == scale;

  @override
  int get hashCode => Object.hash(root, scale);

  @override
  String toString() => 'MusicKey($root, ${scale.name})';
}

/// Một đoạn tone của bài: [start, end).
class KeySegment {
  const KeySegment({
    required this.start,
    required this.end,
    required this.key,
    this.confidence = 0,
  });

  final Duration start;
  final Duration end;
  final MusicKey key;

  /// 0..1 (bỏ qua khi đưa vào [ToneCoreEngine.setKeyTimeline]).
  final double confidence;
}

/// Trạng thái phân tích bài (thứ tự khớp `tc_analysis_state`).
enum AnalysisState { idle, running, done, failed, cancelled }

/// Giai đoạn phân tích đang chạy (thứ tự khớp `tc_analysis_stage`).
enum AnalysisStage {
  none,
  decoding,
  loudness,
  resampling,
  separating,
  spectrum,
  key,
  tempo,
  done,
}

class AnalysisStatus {
  const AnalysisStatus({
    required this.state,
    required this.stage,
    required this.progress,
    required this.result,
  });

  final AnalysisState state;
  final AnalysisStage stage;

  /// 0..1, chỉ tăng.
  final double progress;

  /// Mã `tc_result` của lần chạy gần nhất (lý do khi [AnalysisState.failed]).
  final int result;
}

/// Kết quả phân tích một bài: tone (kèm các đoạn chuyển tone), tuning, BPM, độ to.
class SongAnalysis {
  const SongAnalysis({
    required this.duration,
    required this.sampleRate,
    required this.channels,
    required this.key,
    required this.keyConfidence,
    required this.tuningCents,
    required this.bpm,
    required this.bpmConfidence,
    required this.integratedLufs,
    required this.samplePeakDb,
    required this.timeline,
  });

  final Duration duration;
  final int sampleRate;
  final int channels;

  /// Tone chính; null nếu không kết luận được (bài quá ngắn, chỉ có trống…).
  final MusicKey? key;
  final double keyConfidence;

  /// Lệch so với La = 440 Hz (−50…+50); null nếu không đo được.
  final double? tuningCents;
  final double? bpm;
  final double bpmConfidence;

  /// Độ to tích hợp (LUFS, ITU-R BS.1770-4).
  final double? integratedLufs;
  final double samplePeakDb;

  /// Phủ kín bài; nhiều hơn 1 đoạn nghĩa là bài có chuyển tone.
  final List<KeySegment> timeline;

  bool get hasModulation => timeline.length > 1;
}

/// Trạng thái cao độ để vẽ đồng hồ cao độ.
class PitchState {
  const PitchState({
    required this.voiced,
    required this.f0Hz,
    required this.confidence,
    required this.inputNote,
    required this.targetNote,
    required this.centsOff,
    required this.correctionCents,
    required this.enabled,
  });

  /// Đang có giọng hát.
  final bool voiced;
  final double f0Hz;
  final double confidence;

  /// Cao độ người hát dạng số MIDI thực (69 = La 440 Hz).
  final double inputNote;

  /// Nốt đích (MIDI) hoặc null nếu chưa có.
  final int? targetNote;

  /// Người hát lệch bao nhiêu cent so với nốt đích (+ cao, − thấp).
  final double centsOff;

  /// Engine đang dịch bao nhiêu cent.
  final double correctionCents;

  /// Tự chỉnh giọng đang bật.
  final bool enabled;
}

/// Đường âm thanh hiện tại (thứ tự khớp `tc_route`).
enum AudioRoute { unknown, speaker, wired, bluetooth, usb, receiver }

/// API âm thanh của hệ điều hành (thứ tự khớp `tc_audio_api`).
enum AudioApi { none, aaudio, opensles, remoteio }

enum EngineState { stopped, running }

enum ToneCoreEventType {
  none,
  beatLoaded,
  beatEnded,
  routeChanged,
  interrupted,
  latencyDone,
  streamRestarted,
  monitorBlocked,
  keyChanged,

  /// (từ v0.5) Đã render xong cả bài ở tone mới: code = số nửa cung, value = thời gian render (ms).
  transposeReady,
}

T _enumAt<T>(List<T> values, int index, T fallback) =>
    index >= 0 && index < values.length ? values[index] : fallback;

class ToneCoreException implements Exception {
  ToneCoreException(this.operation, this.code);

  final String operation;
  final int code;

  String get codeName => resultName(code);

  static String resultName(int code) =>
      _bindings.tc_result_to_string(code).cast<Utf8>().toDartString();

  @override
  String toString() => 'ToneCoreException: $operation thất bại ($codeName)';
}

DynamicLibrary _openLibrary() {
  // Cho phép test trên máy dev nạp thư viện build bằng CMake.
  final override = Platform.environment['TONECORE_LIB'];
  if (override != null && override.isNotEmpty) {
    return DynamicLibrary.open(override);
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('libtonecore.so');
  }
  if (Platform.isIOS) return DynamicLibrary.open('tonecore.framework/tonecore');
  if (Platform.isMacOS) return DynamicLibrary.open('libtonecore.dylib');
  throw UnsupportedError('ToneCore chưa hỗ trợ ${Platform.operatingSystem}');
}

final DynamicLibrary _library = _openLibrary();
final ToneCoreBindings _bindings = ToneCoreBindings(_library);

void _check(String operation, int code) {
  if (code != TcResult.ok) throw ToneCoreException(operation, code);
}

/// Thông tin thư viện.
abstract final class ToneCore {
  /// (major << 16) | (minor << 8) | patch
  static int get apiVersion => _bindings.tc_get_api_version();

  static String get version =>
      _bindings.tc_get_version_string().cast<Utf8>().toDartString();

  /// true nếu thư viện native khớp major với binding này.
  static bool get isCompatible => (apiVersion >> 16) == kExpectedApiMajor;

  static String get apiVersionString {
    final v = apiVersion;
    return '${v >> 16}.${(v >> 8) & 0xff}.${v & 0xff}';
  }

  /// (từ v0.6) Preset FX theo thể loại — giá trị engine đã chỉnh (bảng mục 6 của spec). Mức tự
  /// chỉnh giọng của thể loại do app đặt riêng.
  static FxPreset genrePreset(Genre genre) {
    return using((arena) {
      final c = arena<tc_fx_preset>();
      _bindings.tc_fx_preset_init(c, genre.index);
      return _presetFromC(c.ref);
    });
  }
}

FxPreset _presetFromC(tc_fx_preset c) => FxPreset(
  eqHighPassHz: c.eq_high_pass_hz,
  eqLowPassHz: c.eq_low_pass_hz,
  eqBands: List.unmodifiable([
    for (var i = 0; i < FxPreset.bandCount; i++)
      EqBand(
        type: EqBandType.fromCode(c.eq_bands[i].type),
        freqHz: c.eq_bands[i].freq_hz,
        gainDb: c.eq_bands[i].gain_db,
        q: c.eq_bands[i].q,
      ),
  ]),
  compEnabled: c.comp_enabled != 0,
  compThresholdDb: c.comp_threshold_db,
  compRatio: c.comp_ratio,
  compKneeDb: c.comp_knee_db,
  compAttackMs: c.comp_attack_ms,
  compReleaseMs: c.comp_release_ms,
  compMakeupDb: c.comp_makeup_db,
  deessEnabled: c.deess_enabled != 0,
  deessThresholdDb: c.deess_threshold_db,
  deessMaxReductionDb: c.deess_max_reduction_db,
  delayDivision: _enumAt(
    DelayDivision.values,
    c.delay_division,
    DelayDivision.off,
  ),
  delayMs: c.delay_ms,
  delayFeedback: c.delay_feedback,
  delayMix: c.delay_mix,
  delayLowPassHz: c.delay_low_pass_hz,
  delayHighPassHz: c.delay_high_pass_hz,
  reverbEnabled: c.reverb_enabled != 0,
  reverbType: _enumAt(ReverbType.values, c.reverb_type, ReverbType.hall),
  reverbDecayS: c.reverb_decay_s,
  reverbPreDelayMs: c.reverb_pre_delay_ms,
  reverbDamping: c.reverb_damping,
  reverbMix: c.reverb_mix,
);

void _presetToC(FxPreset p, Pointer<tc_fx_preset> out) {
  final c = out.ref
    ..struct_size = sizeOf<tc_fx_preset>()
    ..eq_high_pass_hz = p.eqHighPassHz
    ..eq_low_pass_hz = p.eqLowPassHz
    ..comp_enabled = p.compEnabled ? 1 : 0
    ..comp_threshold_db = p.compThresholdDb
    ..comp_ratio = p.compRatio
    ..comp_knee_db = p.compKneeDb
    ..comp_attack_ms = p.compAttackMs
    ..comp_release_ms = p.compReleaseMs
    ..comp_makeup_db = p.compMakeupDb
    ..deess_enabled = p.deessEnabled ? 1 : 0
    ..deess_threshold_db = p.deessThresholdDb
    ..deess_max_reduction_db = p.deessMaxReductionDb
    ..delay_division = p.delayDivision.index
    ..delay_ms = p.delayMs
    ..delay_feedback = p.delayFeedback
    ..delay_mix = p.delayMix
    ..delay_low_pass_hz = p.delayLowPassHz
    ..delay_high_pass_hz = p.delayHighPassHz
    ..reverb_enabled = p.reverbEnabled ? 1 : 0
    ..reverb_type = p.reverbType.index
    ..reverb_decay_s = p.reverbDecayS
    ..reverb_pre_delay_ms = p.reverbPreDelayMs
    ..reverb_damping = p.reverbDamping
    ..reverb_mix = p.reverbMix;
  for (var i = 0; i < FxPreset.bandCount; i++) {
    final b = i < p.eqBands.length ? p.eqBands[i] : const EqBand();
    c.eq_bands[i]
      ..type = b.type.code
      ..freq_hz = b.freqHz
      ..gain_db = b.gainDb
      ..q = b.q;
  }
}

class EngineMetrics {
  const EngineMetrics({
    required this.state,
    required this.sampleRate,
    required this.framesProcessed,
    required this.callbacks,
    required this.xruns,
    required this.cpuLoad,
    required this.bufferFrames,
    required this.burstFrames,
    required this.reportedInputLatencyMs,
    required this.reportedOutputLatencyMs,
    required this.route,
    required this.audioApi,
    required this.monitorActive,
    required this.inputChannels,
    required this.cpuLoadPeak,
    required this.algorithmLatencyFrames,
    this.fxLoad = 0,
    this.fxChains = 1,
  });

  final EngineState state;
  final int sampleRate;
  final int framesProcessed;
  final int callbacks;
  final int xruns;

  /// 0..1 – tỉ lệ thời gian xử lý trên thời lượng block gần nhất.
  final double cpuLoad;
  final int bufferFrames;
  final int burstFrames;
  final double reportedInputLatencyMs;
  final double reportedOutputLatencyMs;
  final AudioRoute route;
  final AudioApi audioApi;
  final bool monitorActive;
  final int inputChannels;

  /// 0..1 – tải cao nhất trong 1 giây gần nhất.
  final double cpuLoadPeak;

  /// Độ trễ thêm do xử lý (mẫu): 48 (limiter đầu ra 1 ms, từ v0.6) + 480 khi bật tự chỉnh giọng.
  final int algorithmLatencyFrames;

  /// (từ v0.6) 0..1 – phần thời gian của block dành cho chuỗi FX giọng (đã gồm trong [cpuLoad]).
  final double fxLoad;

  /// (từ v0.6) Số chuỗi FX đang chạy: 1 + số chuỗi cũ còn ngân đuôi vang sau khi đổi preset.
  final int fxChains;

  double get algorithmLatencyMs =>
      sampleRate == 0 ? 0 : 1000.0 * algorithmLatencyFrames / sampleRate;

  /// Độ trễ buffer đầu ra (ms) theo cấu hình hiện tại.
  double get bufferMs =>
      sampleRate == 0 ? 0 : 1000.0 * bufferFrames / sampleRate;
}

class PlaybackState {
  const PlaybackState({
    required this.hasBeat,
    required this.playing,
    required this.position,
    required this.duration,
    this.transpose = 0,
    this.audibleTranspose = 0,
    this.transposeProgress = 1.0,
  });

  final bool hasBeat;
  final bool playing;
  final Duration position;
  final Duration duration;

  /// (từ v0.5) Số nửa cung đã yêu cầu ([ToneCoreEngine.transpose]).
  final int transpose;

  /// Số nửa cung beat đang NGHE — đổi sau ~0,1 s khi bản mới sẵn tại vị trí phát.
  final int audibleTranspose;

  /// 0..1: phần bài đã render ở tone yêu cầu (1 khi [transpose] = 0).
  final double transposeProgress;

  /// Đã đổi tone nhưng bản mới chưa nghe được (đang render đoạn tại vị trí phát).
  bool get transposePending => transpose != audibleTranspose;
}

class LatencyResult {
  const LatencyResult({
    required this.valid,
    required this.latencyMs,
    required this.latencyFrames,
    required this.confidence,
    required this.validBursts,
    required this.totalBursts,
    required this.spreadMs,
    required this.reportedInputLatencyMs,
    required this.reportedOutputLatencyMs,
    required this.bufferFrames,
    required this.burstFrames,
    required this.route,
  });

  final bool valid;
  final double latencyMs;
  final double latencyFrames;
  final double confidence;
  final int validBursts;
  final int totalBursts;
  final double spreadMs;
  final double reportedInputLatencyMs;
  final double reportedOutputLatencyMs;
  final int bufferFrames;
  final int burstFrames;
  final AudioRoute route;

  /// Tổng độ trễ hệ điều hành tự báo (để so với số đo thật).
  double get reportedRoundTripMs =>
      reportedInputLatencyMs + reportedOutputLatencyMs;
}

class ToneCoreEvent {
  const ToneCoreEvent(this.type, this.code, this.value);

  final ToneCoreEventType type;
  final int code;
  final double value;

  bool get ok => code == TcResult.ok;

  /// Với [ToneCoreEventType.keyChanged]: key chỉnh giọng đang áp dụng vừa đổi — theo timeline, hoặc
  /// (từ v0.5) beat bắt đầu nghe ở tone mới. Đã cộng số nửa cung đổi tone.
  MusicKey? get key => type == ToneCoreEventType.keyChanged
      ? MusicKey(
          code,
          _enumAt(MusicScale.values, value.round(), MusicScale.chromatic),
        )
      : null;

  @override
  String toString() => 'ToneCoreEvent($type, code=$code, value=$value)';
}

/// Một phiên bản engine. Gọi [dispose] khi xong; nếu quên, bộ thu gom rác sẽ tự huỷ.
class ToneCoreEngine implements Finalizable {
  ToneCoreEngine._(
    this._handle,
    this.inputChannels,
    this.outputChannels,
    this.maxBlockFrames,
  ) {
    _finalizer.attach(this, _handle.cast(), detach: this);
  }

  /// [platform] = true: dùng thiết bị âm thanh thật (Oboe/RemoteIO).
  /// false: backend NULL, xử lý bằng [processOffline] (test).
  factory ToneCoreEngine.create({
    int sampleRate = 48000,
    int maxBlockFrames = 1024,
    int inputChannels = 1,
    int outputChannels = 2,
    bool platform = false,
  }) {
    return using((arena) {
      final config = arena<tc_engine_config>();
      _bindings.tc_engine_config_init(config);
      config.ref
        ..sample_rate = sampleRate
        ..max_block_frames = maxBlockFrames
        ..input_channels = inputChannels
        ..output_channels = outputChannels
        ..backend = platform
            ? tc_backend.TC_BACKEND_PLATFORM
            : tc_backend.TC_BACKEND_NULL;
      final out = arena<Pointer<tc_engine>>();
      _check('tc_engine_create', _bindings.tc_engine_create(config, out));
      return ToneCoreEngine._(
        out.value,
        inputChannels,
        outputChannels,
        maxBlockFrames,
      );
    });
  }

  /// Engine dùng thiết bị âm thanh của máy.
  factory ToneCoreEngine.platform() => ToneCoreEngine.create(platform: true);

  static final _finalizer = NativeFinalizer(
    _bindings.addresses.tc_engine_destroy.cast(),
  );

  final Pointer<tc_engine> _handle;
  final int inputChannels;
  final int outputChannels;
  final int maxBlockFrames;
  bool _disposed = false;

  Pointer<tc_engine> get _h {
    if (_disposed) throw StateError('ToneCoreEngine đã bị dispose');
    return _handle;
  }

  // ---- Vòng đời ----

  void start() => _check('tc_engine_start', _bindings.tc_engine_start(_h));

  void stop() => _check('tc_engine_stop', _bindings.tc_engine_stop(_h));

  void dispose() {
    if (_disposed) return;
    _pollTimer?.cancel();
    _pollTimer = null;
    _events?.close();
    _events = null;
    _disposed = true;
    _finalizer.detach(this);
    _bindings.tc_engine_destroy(_handle);
  }

  // ---- Tham số ----

  void setParam(int node, int param, double value) =>
      _check('tc_set_param', _bindings.tc_set_param(_h, node, param, value));

  double getParam(int node, int param) {
    return using((arena) {
      final out = arena<Float>();
      _check('tc_get_param', _bindings.tc_get_param(_h, node, param, out));
      return out.value;
    });
  }

  /// Bật/tắt nghe lại giọng qua tai nghe.
  set monitorEnabled(bool on) =>
      setParam(TcNode.monitor, TcParam.enabled, on ? 1.0 : 0.0);
  bool get monitorEnabled => getParam(TcNode.monitor, TcParam.enabled) >= 0.5;

  set monitorGain(double g) => setParam(TcNode.monitor, TcParam.gain, g);
  double get monitorGain => getParam(TcNode.monitor, TcParam.gain);

  set beatGain(double g) => setParam(TcNode.beat, TcParam.gain, g);
  double get beatGain => getParam(TcNode.beat, TcParam.gain);

  // ---- Tự chỉnh giọng ----

  /// Bật/tắt tự chỉnh giọng (khi bật giọng nghe lại trễ thêm 10 ms).
  set pitchCorrectionEnabled(bool on) =>
      setParam(TcNode.pitch, TcParam.enabled, on ? 1.0 : 0.0);
  bool get pitchCorrectionEnabled =>
      getParam(TcNode.pitch, TcParam.enabled) >= 0.5;

  set key(MusicKey k) =>
      _check('tc_set_key', _bindings.tc_set_key(_h, k.root, k.scale.index));

  /// Key của beat GỐC: chọn tay, hoặc đoạn timeline đang áp dụng (chưa cộng [transpose]).
  MusicKey get key {
    return using((arena) {
      final root = arena<Int32>();
      final scale = arena<Int32>();
      _check('tc_get_key', _bindings.tc_get_key(_h, root, scale));
      return MusicKey(
        root.value,
        _enumAt(MusicScale.values, scale.value, MusicScale.chromatic),
      );
    });
  }

  /// (từ v0.5) Key đang dùng để chỉnh giọng = [key] + số nửa cung beat đang nghe. Hiển thị cái này.
  MusicKey get appliedKey {
    return using((arena) {
      final root = arena<Int32>();
      final scale = arena<Int32>();
      _check(
        'tc_get_applied_key',
        _bindings.tc_get_applied_key(_h, root, scale),
      );
      return MusicKey(
        root.value,
        _enumAt(MusicScale.values, scale.value, MusicScale.chromatic),
      );
    });
  }

  /// (từ v0.5) Đổi tone beat −6…+6 nửa cung, giữ tempo. Render ở luồng nền từ vị trí đang phát;
  /// nghe tone mới sau ~0,1 s (crossfade 20 ms). Key chỉnh giọng tự cộng theo khi bản mới được
  /// nghe (sự kiện [ToneCoreEventType.keyChanged]). Cần đã nạp beat; nạp beat mới → về 0.
  set transpose(int semitones) =>
      _check('tc_set_transpose', _bindings.tc_set_transpose(_h, semitones));

  int get transpose {
    return using((arena) {
      final out = arena<Int32>();
      _check('tc_get_transpose', _bindings.tc_get_transpose(_h, out));
      return out.value;
    });
  }

  /// Lưới nốt của tự chỉnh giọng dời so với La = 440 Hz (−100…100 cent), thường lấy từ
  /// [SongAnalysis.tuningCents] để giọng được kéo về đúng cao độ của beat.
  set tuningCents(double cents) =>
      _check('tc_set_tuning', _bindings.tc_set_tuning(_h, cents));

  double get tuningCents {
    return using((arena) {
      final out = arena<Float>();
      _check('tc_get_tuning', _bindings.tc_get_tuning(_h, out));
      return out.value;
    });
  }

  /// Engine tự đổi key theo vị trí beat (cả khi tua); mỗi lần đổi có sự kiện
  /// [ToneCoreEventType.keyChanged]. Gán [key] (chọn tay) sẽ tắt chế độ này.
  void setKeyTimeline(List<KeySegment> segments) {
    using((arena) {
      final buf = segments.isEmpty
          ? nullptr
          : arena<tc_key_segment>(segments.length);
      for (var i = 0; i < segments.length; i++) {
        final s = segments[i];
        buf[i]
          ..start_ms = s.start.inMicroseconds / 1000.0
          ..end_ms = s.end.inMicroseconds / 1000.0
          ..root = s.key.root
          ..scale = s.key.scale.index
          ..confidence = s.confidence;
      }
      _check(
        'tc_set_key_timeline',
        _bindings.tc_set_key_timeline(_h, buf, segments.length),
      );
    });
  }

  /// Thôi đổi key theo bài, quay về key chọn tay gần nhất.
  void clearKeyTimeline() => setKeyTimeline(const []);

  /// Áp một trong 3 mức chỉnh (ghi đè retune/humanize/flex/vibrato/hysteresis).
  void applyCorrectionPreset(CorrectionPreset p) => _check(
    'tc_set_correction_preset',
    _bindings.tc_set_correction_preset(_h, p.index),
  );

  PitchState get pitchState {
    return using((arena) {
      final s = arena<tc_pitch_state>();
      s.ref.struct_size = sizeOf<tc_pitch_state>();
      _check('tc_get_pitch_state', _bindings.tc_get_pitch_state(_h, s));
      final v = s.ref;
      return PitchState(
        voiced: v.voiced != 0,
        f0Hz: v.f0_hz,
        confidence: v.confidence,
        inputNote: v.input_note,
        targetNote: v.target_note >= 0 ? v.target_note : null,
        centsOff: v.cents_off,
        correctionCents: v.correction_cents,
        enabled: v.enabled != 0,
      );
    });
  }

  // ---- FX giọng (từ v0.6) ----

  /// Đổi preset FX khi đang hát, không click: khác cấu trúc (bật / tắt khối, thời gian delay, kiểu
  /// phòng…) → phần khô crossfade 15 ms, đuôi vang cũ ngân nốt; cùng cấu trúc → thông số trượt dần.
  /// Giá trị NaN / vô hạn → [ToneCoreException] ([TcResult.invalidArg]); ngoài khoảng → bị kẹp.
  void applyFxPreset(FxPreset preset) {
    using((arena) {
      final c = arena<tc_fx_preset>();
      _presetToC(preset, c);
      _check('tc_apply_fx_preset', _bindings.tc_apply_fx_preset(_h, c));
    });
  }

  /// Preset đang đặt (sau khi kẹp), kể cả khi [fxEnabled] = false.
  FxPreset get fxPreset {
    return using((arena) {
      final c = arena<tc_fx_preset>();
      c.ref.struct_size = sizeOf<tc_fx_preset>();
      _check('tc_get_fx_preset', _bindings.tc_get_fx_preset(_h, c));
      return _presetFromC(c.ref);
    });
  }

  /// Tắt = giọng khô (đuôi vang đang có vẫn ngân nốt); bật lại = preset đã đặt. Mặc định bật.
  set fxEnabled(bool on) =>
      setParam(TcNode.fx, TcParam.enabled, on ? 1.0 : 0.0);
  bool get fxEnabled => getParam(TcNode.fx, TcParam.enabled) >= 0.5;

  /// "Vang": nhân phần vọng + vang của preset, 0..2 (1 = đúng preset), trượt 20 ms.
  set fxMix(double scale) => setParam(TcNode.fx, TcParam.mix, scale);
  double get fxMix => getParam(TcNode.fx, TcParam.mix);

  /// Nhịp bài (BPM) cho delay theo nốt; ≤ 0 = không rõ (engine dùng 100). Đổi khi delay đang bật →
  /// tiếng vọng mới theo nhịp mới, vọng cũ ngân nốt.
  set tempo(double bpm) =>
      _check('tc_set_tempo', _bindings.tc_set_tempo(_h, bpm));

  double get tempo {
    return using((arena) {
      final out = arena<Float>();
      _check('tc_get_tempo', _bindings.tc_get_tempo(_h, out));
      return out.value;
    });
  }

  /// Cổng chặn tiếng ồn micro (sau lọc 80 Hz, trước dò cao độ). Mặc định tắt.
  set gateEnabled(bool on) =>
      setParam(TcNode.gate, TcParam.enabled, on ? 1.0 : 0.0);
  bool get gateEnabled => getParam(TcNode.gate, TcParam.enabled) >= 0.5;

  /// −80..0 dBFS: cổng mở khi tiếng to hơn ngưỡng; đóng thì chỉ giảm −30 dB.
  set gateThresholdDb(double db) =>
      setParam(TcNode.gate, TcParam.thresholdDb, db);
  double get gateThresholdDb => getParam(TcNode.gate, TcParam.thresholdDb);

  // ---- Beat ----

  /// Nạp beat ở luồng nền; kết quả đến qua sự kiện [ToneCoreEventType.beatLoaded].
  void loadBeat(String path) {
    using((arena) {
      final p = path.toNativeUtf8(allocator: arena);
      _check('tc_load_beat', _bindings.tc_load_beat(_h, p.cast()));
    });
  }

  /// Nạp beat và chờ kết quả.
  Future<Duration> loadBeatAndWait(
    String path, {
    Duration timeout = const Duration(seconds: 60),
  }) async {
    final done = events
        .firstWhere((e) => e.type == ToneCoreEventType.beatLoaded)
        .timeout(timeout);
    loadBeat(path);
    final e = await done;
    if (!e.ok) throw ToneCoreException('tc_load_beat', e.code);
    return Duration(microseconds: (e.value * 1000).round());
  }

  void play() => _check('tc_play', _bindings.tc_play(_h));

  void pause() => _check('tc_pause', _bindings.tc_pause(_h));

  void seek(Duration position) => _check(
    'tc_seek',
    _bindings.tc_seek(_h, position.inMicroseconds / 1000.0),
  );

  PlaybackState get playback {
    return using((arena) {
      final s = arena<tc_playback_state>();
      s.ref.struct_size = sizeOf<tc_playback_state>();
      _check('tc_get_playback_state', _bindings.tc_get_playback_state(_h, s));
      return PlaybackState(
        hasBeat: s.ref.has_beat != 0,
        playing: s.ref.playing != 0,
        position: Duration(microseconds: (s.ref.position_ms * 1000).round()),
        duration: Duration(microseconds: (s.ref.duration_ms * 1000).round()),
        transpose: s.ref.transpose,
        audibleTranspose: s.ref.audible_transpose,
        transposeProgress: s.ref.transpose_progress,
      );
    });
  }

  // ---- Route ----

  void setRouteHint(AudioRoute route) =>
      _check('tc_set_route_hint', _bindings.tc_set_route_hint(_h, route.index));

  // ---- Đo độ trễ ----

  void startLatencyMeasurement() => _check(
    'tc_measure_latency_start',
    _bindings.tc_measure_latency_start(_h),
  );

  LatencyResult get latencyResult {
    return using((arena) {
      final r = arena<tc_latency_result>();
      r.ref.struct_size = sizeOf<tc_latency_result>();
      _check('tc_get_latency_result', _bindings.tc_get_latency_result(_h, r));
      final v = r.ref;
      return LatencyResult(
        valid: v.valid != 0,
        latencyMs: v.latency_ms,
        latencyFrames: v.latency_frames,
        confidence: v.confidence,
        validBursts: v.valid_bursts,
        totalBursts: v.total_bursts,
        spreadMs: v.spread_ms,
        reportedInputLatencyMs: v.reported_input_latency_ms,
        reportedOutputLatencyMs: v.reported_output_latency_ms,
        bufferFrames: v.buffer_frames,
        burstFrames: v.burst_frames,
        route: _enumAt(AudioRoute.values, v.route, AudioRoute.unknown),
      );
    });
  }

  /// Đo độ trễ round-trip (~2 s) và chờ kết quả.
  Future<LatencyResult> measureLatency({
    Duration timeout = const Duration(seconds: 20),
  }) async {
    final done = events
        .firstWhere((e) => e.type == ToneCoreEventType.latencyDone)
        .timeout(timeout);
    startLatencyMeasurement();
    await done;
    return latencyResult;
  }

  // ---- Sự kiện ----

  /// Lấy các sự kiện đang chờ (gọi định kỳ, hoặc dùng [events]).
  List<ToneCoreEvent> pollEvents() {
    return using((arena) {
      const max = 32;
      final buf = arena<tc_event>(max);
      final n = _bindings.tc_poll_events(_h, buf, max);
      if (n < 0) throw ToneCoreException('tc_poll_events', n);
      return [
        for (var i = 0; i < n; i++)
          ToneCoreEvent(
            _enumAt(
              ToneCoreEventType.values,
              buf[i].type,
              ToneCoreEventType.none,
            ),
            buf[i].code,
            buf[i].value,
          ),
      ];
    });
  }

  StreamController<ToneCoreEvent>? _events;
  Timer? _pollTimer;

  /// Luồng sự kiện; engine được hỏi mỗi ~16 ms khi có người nghe.
  Stream<ToneCoreEvent> get events {
    _events ??= StreamController<ToneCoreEvent>.broadcast(
      onListen: () {
        _pollTimer ??= Timer.periodic(const Duration(milliseconds: 16), (_) {
          if (_disposed) return;
          for (final e in pollEvents()) {
            _events?.add(e);
          }
        });
      },
      onCancel: () {
        _pollTimer?.cancel();
        _pollTimer = null;
      },
    );
    return _events!.stream;
  }

  // ---- Xử lý offline (backend NULL) ----

  /// Xử lý offline một block. [input] interleaved, có thể null.
  Float32List processOffline(Float32List? input, int frames) {
    if (frames < 0 || frames > maxBlockFrames) {
      throw RangeError.range(frames, 0, maxBlockFrames, 'frames');
    }
    if (input != null && input.length < frames * inputChannels) {
      throw ArgumentError('input cần ít nhất ${frames * inputChannels} mẫu');
    }
    return using((arena) {
      Pointer<Float> inPtr = nullptr;
      if (input != null) {
        inPtr = arena<Float>(frames * inputChannels);
        inPtr
            .asTypedList(frames * inputChannels)
            .setAll(0, input.take(frames * inputChannels));
      }
      final outPtr = arena<Float>(frames * outputChannels);
      _check(
        'tc_engine_process_offline',
        _bindings.tc_engine_process_offline(_h, inPtr, outPtr, frames),
      );
      return Float32List.fromList(outPtr.asTypedList(frames * outputChannels));
    });
  }

  // ---- Đo lường ----

  EngineMetrics get metrics {
    return using((arena) {
      final m = arena<tc_metrics>();
      m.ref.struct_size = sizeOf<tc_metrics>();
      _check('tc_get_metrics', _bindings.tc_get_metrics(_h, m));
      final v = m.ref;
      return EngineMetrics(
        state: v.state == tc_engine_state.TC_STATE_RUNNING
            ? EngineState.running
            : EngineState.stopped,
        sampleRate: v.sample_rate,
        framesProcessed: v.frames_processed,
        callbacks: v.callbacks,
        xruns: v.xruns,
        cpuLoad: v.cpu_load,
        bufferFrames: v.buffer_frames,
        burstFrames: v.burst_frames,
        reportedInputLatencyMs: v.reported_input_latency_ms,
        reportedOutputLatencyMs: v.reported_output_latency_ms,
        route: _enumAt(AudioRoute.values, v.route, AudioRoute.unknown),
        audioApi: _enumAt(AudioApi.values, v.audio_api, AudioApi.none),
        monitorActive: v.monitor_active != 0,
        inputChannels: v.input_channels,
        cpuLoadPeak: v.cpu_load_peak,
        algorithmLatencyFrames: v.algorithm_latency_frames,
        fxLoad: v.fx_load,
        fxChains: v.fx_chains,
      );
    });
  }
}

/// Phân tích bài ở luồng nền (tone, chuyển tone, tuning, BPM, độ to) — không cần engine chạy.
/// Gọi [dispose] khi xong; nếu quên, bộ thu gom rác sẽ tự huỷ (và dừng việc đang chạy).
class SongAnalyzer implements Finalizable {
  SongAnalyzer._(this._handle) {
    _finalizer.attach(this, _handle.cast(), detach: this);
  }

  factory SongAnalyzer.create() {
    return using((arena) {
      final out = arena<Pointer<tc_analyzer>>();
      _check('tc_analyzer_create', _bindings.tc_analyzer_create(out));
      return SongAnalyzer._(out.value);
    });
  }

  static final _finalizer = NativeFinalizer(
    _bindings.addresses.tc_analyzer_destroy.cast(),
  );

  final Pointer<tc_analyzer> _handle;
  bool _disposed = false;

  Pointer<tc_analyzer> get _h {
    if (_disposed) throw StateError('SongAnalyzer đã bị dispose');
    return _handle;
  }

  /// Bắt đầu phân tích (lỗi đọc file báo qua [status], không ném ở đây).
  void start(String path) {
    using((arena) {
      final p = path.toNativeUtf8(allocator: arena);
      _check('tc_analyzer_start', _bindings.tc_analyzer_start(_h, p.cast()));
    });
  }

  /// Yêu cầu dừng; [status] chuyển sang [AnalysisState.cancelled] khi luồng nền dừng.
  void cancel() =>
      _check('tc_analyzer_cancel', _bindings.tc_analyzer_cancel(_h));

  AnalysisStatus get status {
    return using((arena) {
      final s = arena<tc_analysis_status>();
      s.ref.struct_size = sizeOf<tc_analysis_status>();
      _check('tc_analyzer_get_status', _bindings.tc_analyzer_get_status(_h, s));
      final v = s.ref;
      return AnalysisStatus(
        state: _enumAt(AnalysisState.values, v.state, AnalysisState.idle),
        stage: _enumAt(AnalysisStage.values, v.stage, AnalysisStage.none),
        progress: v.progress,
        result: v.result,
      );
    });
  }

  /// Chỉ đọc được khi [status] là [AnalysisState.done].
  SongAnalysis get result {
    return using((arena) {
      final r = arena<tc_analysis_result>();
      r.ref.struct_size = sizeOf<tc_analysis_result>();
      _check('tc_analyzer_get_result', _bindings.tc_analyzer_get_result(_h, r));
      final v = r.ref;
      final count = v.timeline_count;
      final segs = count == 0 ? nullptr : arena<tc_key_segment>(count);
      final n = _bindings.tc_analyzer_get_key_timeline(_h, segs, count);
      if (n < 0) throw ToneCoreException('tc_analyzer_get_key_timeline', n);
      MusicKey keyOf(int root, int scale) => MusicKey(
        root,
        _enumAt(MusicScale.values, scale, MusicScale.chromatic),
      );
      Duration ms(double v) => Duration(microseconds: (v * 1000).round());
      return SongAnalysis(
        duration: ms(v.duration_ms),
        sampleRate: v.sample_rate,
        channels: v.channels,
        key: v.has_key != 0 ? keyOf(v.key_root, v.key_scale) : null,
        keyConfidence: v.key_confidence,
        tuningCents: v.has_tuning != 0 ? v.tuning_cents : null,
        bpm: v.has_bpm != 0 ? v.bpm : null,
        bpmConfidence: v.bpm_confidence,
        integratedLufs: v.has_loudness != 0 ? v.integrated_lufs : null,
        samplePeakDb: v.sample_peak_db,
        timeline: [
          for (var i = 0; i < n; i++)
            KeySegment(
              start: ms(segs[i].start_ms),
              end: ms(segs[i].end_ms),
              key: keyOf(segs[i].root, segs[i].scale),
              confidence: segs[i].confidence,
            ),
        ],
      );
    });
  }

  /// Phân tích và chờ kết quả, báo tiến độ qua [onProgress] mỗi [pollInterval].
  /// Bị huỷ → [ToneCoreException] mã [TcResult.cancelled]; lỗi file → mã lỗi tương ứng.
  Future<SongAnalysis> analyze(
    String path, {
    void Function(AnalysisStatus status)? onProgress,
    Duration pollInterval = const Duration(milliseconds: 100),
    Duration timeout = const Duration(minutes: 5),
  }) async {
    start(path);
    final deadline = DateTime.now().add(timeout);
    while (true) {
      final s = status;
      onProgress?.call(s);
      switch (s.state) {
        case AnalysisState.done:
          return result;
        case AnalysisState.failed:
        case AnalysisState.cancelled:
          throw ToneCoreException('tc_analyzer', s.result);
        case AnalysisState.idle:
        case AnalysisState.running:
          break;
      }
      if (DateTime.now().isAfter(deadline)) {
        cancel();
        throw TimeoutException('Phân tích bài quá lâu', timeout);
      }
      await Future<void>.delayed(pollInterval);
      if (_disposed) throw StateError('SongAnalyzer đã bị dispose');
    }
  }

  /// Dừng việc đang chạy (chờ luồng nền dừng, thường < 50 ms) và giải phóng.
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _finalizer.detach(this);
    _bindings.tc_analyzer_destroy(_handle);
  }
}
