// Lớp Dart thân thiện bọc C API của ToneCore (xem docs/adr/001-c-api-ffi.md).

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

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
}

abstract final class TcNode {
  static const int master = tc_node_id.TC_NODE_MASTER;
  static const int monitor = tc_node_id.TC_NODE_MONITOR;
  static const int beat = tc_node_id.TC_NODE_BEAT;
  static const int pitch = tc_node_id.TC_NODE_PITCH;
}

abstract final class TcParam {
  static const int gain = tc_param_id.TC_PARAM_GAIN;
  static const int enabled = tc_param_id.TC_PARAM_ENABLED;
  static const int retuneMs = tc_param_id.TC_PARAM_RETUNE_MS;
  static const int humanize = tc_param_id.TC_PARAM_HUMANIZE;
  static const int flexCents = tc_param_id.TC_PARAM_FLEX_CENTS;
  static const int vibratoPreserve = tc_param_id.TC_PARAM_VIBRATO_PRESERVE;
  static const int hysteresisCents = tc_param_id.TC_PARAM_HYSTERESIS_CENTS;
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

  /// Độ trễ thêm do xử lý (mẫu): 0 khi tắt tự chỉnh giọng.
  final int algorithmLatencyFrames;

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
  });

  final bool hasBeat;
  final bool playing;
  final Duration position;
  final Duration duration;
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
      );
    });
  }
}
