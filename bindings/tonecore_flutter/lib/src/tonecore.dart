// Lớp Dart thân thiện bọc C API của ToneCore (xem docs/adr/001-c-api-ffi.md).

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
}

abstract final class TcNode {
  static const int master = tc_node_id.TC_NODE_MASTER;
}

abstract final class TcParam {
  static const int gain = tc_param_id.TC_PARAM_GAIN;
}

class ToneCoreException implements Exception {
  ToneCoreException(this.operation, this.code);

  final String operation;
  final int code;

  String get codeName =>
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

enum EngineState { stopped, running }

class EngineMetrics {
  const EngineMetrics({
    required this.state,
    required this.sampleRate,
    required this.framesProcessed,
    required this.callbacks,
    required this.xruns,
    required this.cpuLoad,
  });

  final EngineState state;
  final int sampleRate;
  final int framesProcessed;
  final int callbacks;
  final int xruns;

  /// 0..1 – tỉ lệ thời gian xử lý trên thời lượng block gần nhất.
  final double cpuLoad;
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

  factory ToneCoreEngine.create({
    int sampleRate = 48000,
    int maxBlockFrames = 1024,
    int inputChannels = 1,
    int outputChannels = 2,
  }) {
    return using((arena) {
      final config = arena<tc_engine_config>();
      _bindings.tc_engine_config_init(config);
      config.ref
        ..sample_rate = sampleRate
        ..max_block_frames = maxBlockFrames
        ..input_channels = inputChannels
        ..output_channels = outputChannels;
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

  void start() => _check('tc_engine_start', _bindings.tc_engine_start(_h));

  void stop() => _check('tc_engine_stop', _bindings.tc_engine_stop(_h));

  void setParam(int node, int param, double value) =>
      _check('tc_set_param', _bindings.tc_set_param(_h, node, param, value));

  double getParam(int node, int param) {
    return using((arena) {
      final out = arena<Float>();
      _check('tc_get_param', _bindings.tc_get_param(_h, node, param, out));
      return out.value;
    });
  }

  /// Xử lý offline một block (chỉ với backend NULL). [input] interleaved, có thể null.
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

  EngineMetrics get metrics {
    return using((arena) {
      final m = arena<tc_metrics>();
      m.ref.struct_size = sizeOf<tc_metrics>();
      _check('tc_get_metrics', _bindings.tc_get_metrics(_h, m));
      return EngineMetrics(
        state: m.ref.state == tc_engine_state.TC_STATE_RUNNING
            ? EngineState.running
            : EngineState.stopped,
        sampleRate: m.ref.sample_rate,
        framesProcessed: m.ref.frames_processed,
        callbacks: m.ref.callbacks,
        xruns: m.ref.xruns,
        cpuLoad: m.ref.cpu_load,
      );
    });
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _finalizer.detach(this);
    _bindings.tc_engine_destroy(_handle);
  }
}
