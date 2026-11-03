import 'dart:async';
import 'dart:io';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/app_settings.dart';
import '../data/latency_history.dart';
import '../engine/audio_service.dart';
import '../engine/latency_measure.dart';
import '../widgets/input_level_meter.dart';

/// Người dùng chọn gì ở bước cuối của hướng dẫn.
enum OnboardingExit { home, addSong, sample }

/// Hướng dẫn lần đầu (M8 B5, docs/m8-plan.md mục 1): Chào → Micro (giải thích rồi mới hỏi quyền; bị
/// từ chối thì mở Cài đặt của máy) → Đo nhanh qua loa → Tai nghe (nhận ra đường tiếng) → Thử giọng
/// (thanh mức micro, nghe lại có vang) → Xong ("Thêm bài hát đầu tiên" / "Hát thử bài mẫu"). Bỏ qua
/// được; xem lại ở Cài đặt → Trợ giúp.
class OnboardingPage extends StatefulWidget {
  const OnboardingPage({
    super.key,
    required this.onFinish,
    this.service,
    this.settings,
    this.latency,
    this.micStatus,
    this.requestMic,
    this.measure,
  });

  final void Function(OnboardingExit exit) onFinish;
  final AudioService? service;
  final AppSettings? settings;
  final LatencyHistory? latency;

  /// Test thay phần native: trạng thái / xin quyền micro, đo độ trễ.
  final Future<MicStatus> Function()? micStatus;
  final Future<bool> Function()? requestMic;
  final Future<LatencyResult?> Function()? measure;

  @override
  State<OnboardingPage> createState() => _OnboardingPageState();
}

enum _Step { welcome, mic, measure, headphones, voice, done }

class _OnboardingPageState extends State<OnboardingPage> {
  late final AudioService _service = widget.service ?? AudioService.instance;
  late final AppSettings _settings = widget.settings ?? AppSettings();
  late final LatencyHistory _latency = widget.latency ?? LatencyHistory();
  _Step _step = _Step.welcome;

  MicStatus? _mic;
  bool _asking = false;
  bool _measuring = false;
  LatencyResult? _result;
  bool _measured = false;
  AudioRoute _route = AudioRoute.unknown;
  double _peakDb = -120;
  bool? _heard; // "Nghe rõ giọng mình chưa?"
  Timer? _timer;
  // Giọng nghe lại có vang ở bước Thử giọng; trả lại thể loại cũ khi rời bước.
  ({
    Genre genre,
    CorrectionPreset? correction,
    double? retuneMs,
    double fxMix,
    bool gateOn,
  })?
  _savedVoice;

  @override
  void dispose() {
    _timer?.cancel();
    _restoreVoice();
    super.dispose();
  }

  Future<void> _go(_Step s) async {
    if (_step == _Step.voice) _restoreVoice();
    setState(() => _step = s);
    switch (s) {
      case _Step.mic:
        final st = await (widget.micStatus ?? SystemPermissions.micStatus)();
        if (mounted) setState(() => _mic = st);
      case _Step.measure || _Step.headphones:
        await _startEngine();
      case _Step.voice:
        _enterVoice(); // đặt trước: engine mở sau vẫn nhận thể loại này
        await _startEngine();
      default:
        break;
    }
  }

  Future<void> _startEngine() async {
    await _service.ensureStarted();
    _timer ??= Timer.periodic(const Duration(milliseconds: 100), (_) {
      final m = _service.engine?.metrics;
      if (m == null || !mounted) return;
      setState(() {
        _route = m.route;
        _peakDb = m.inputPeakDb;
      });
    });
  }

  Future<void> _askMic() async {
    setState(() => _asking = true);
    bool granted;
    final custom = widget.requestMic;
    if (custom != null) {
      granted = await custom();
    } else if (Platform.isIOS) {
      granted = await SystemPermissions.requestMicIOS() ?? true;
    } else {
      granted = await MicPermission.request();
    }
    if (!mounted) return;
    setState(() {
      _asking = false;
      _mic = granted ? MicStatus.granted : MicStatus.denied;
    });
    if (granted) unawaited(_go(_Step.measure));
  }

  Future<void> _measure() async {
    setState(() => _measuring = true);
    LatencyResult? r;
    try {
      final custom = widget.measure;
      if (custom != null) {
        r = await custom();
      } else if (_service.engine case final e?) {
        (r, _) = await measureAndSaveLatency(
          e,
          store: _latency,
          history: await _latency.load(),
          device: await DeviceLabel.load(),
        );
      }
    } catch (_) {
      r = null;
    }
    if (!mounted) return;
    setState(() {
      _measuring = false;
      _measured = true;
      _result = r;
    });
  }

  void _enterVoice() {
    final s = _service;
    _savedVoice ??= (
      genre: s.genre,
      correction: s.correction,
      retuneMs: s.correctionRetuneMs,
      fxMix: s.fxMix,
      gateOn: s.gateOn,
    );
    s.setGenre(Genre.ballad); // có vang để nghe thử
    setState(() => _heard = null);
  }

  void _restoreVoice() {
    final v = _savedVoice;
    if (v == null) return;
    _savedVoice = null;
    _service.applyVoiceSettings(
      genre: v.genre,
      correction: v.correction,
      retuneMs: v.retuneMs,
      fxMix: v.fxMix,
      gateOn: v.gateOn,
    );
  }

  Future<void> _finish(OnboardingExit exit) async {
    _restoreVoice();
    await _settings.setOnboardingDone(true);
    widget.onFinish(exit);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        automaticallyImplyLeading: false,
        title: Text('Bước ${_step.index + 1}/${_Step.values.length}'),
        actions: [
          if (_step != _Step.done)
            TextButton(
              onPressed: () => _finish(OnboardingExit.home),
              child: const Text('Bỏ qua'),
            ),
        ],
      ),
      body: SafeArea(
        child: _layout(switch (_step) {
          _Step.welcome => _welcome(context),
          _Step.mic => _micStep(context),
          _Step.measure => _measureStep(context),
          _Step.headphones => _headphones(context),
          _Step.voice => _voice(context),
          _Step.done => _done(context),
        }),
      ),
    );
  }

  /// Nội dung cuộn được; các nút ([_Action]) luôn ở đáy màn hình (máy nhỏ, chữ to vẫn thấy ngay).
  Widget _layout(List<Widget> all) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      Expanded(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(24, 8, 24, 16),
          children: [
            for (final w in all)
              if (w is! _Action) w,
          ],
        ),
      ),
      Padding(
        padding: const EdgeInsets.fromLTRB(24, 0, 24, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            for (final w in all)
              if (w is _Action) w,
          ],
        ),
      ),
    ],
  );

  Widget _icon(BuildContext context, IconData icon, {Color? color}) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 16),
    child: Icon(
      icon,
      size: 80,
      color: color ?? Theme.of(context).colorScheme.primary,
    ),
  );

  Widget _title(BuildContext context, String text) => Text(
    text,
    textAlign: TextAlign.center,
    style: Theme.of(context).textTheme.headlineSmall
        ?.copyWith(fontWeight: FontWeight.w700),
  );

  Widget _body(BuildContext context, String text) => Padding(
    padding: const EdgeInsets.only(top: 8),
    child: Text(
      text,
      textAlign: TextAlign.center,
      style: Theme.of(context).textTheme.bodyLarge,
    ),
  );

  Widget _primary(String label, VoidCallback? onPressed, {Key? key}) => _Action(
    Padding(
      padding: const EdgeInsets.only(top: 8),
      child: FilledButton(
        key: key,
        onPressed: onPressed,
        style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(56)),
        child: Text(label),
      ),
    ),
  );

  Widget _secondary(String label, VoidCallback? onPressed) =>
      _Action(TextButton(onPressed: onPressed, child: Text(label)));

  List<Widget> _welcome(BuildContext context) {
    final theme = Theme.of(context);
    Widget point(IconData icon, String text) => Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        children: [
          Icon(icon, color: theme.colorScheme.primary, size: 32),
          const SizedBox(width: 16),
          Expanded(child: Text(text, style: theme.textTheme.titleMedium)),
        ],
      ),
    );
    return [
      const SizedBox(height: 24),
      Text(
        'ChuẩnTone',
        textAlign: TextAlign.center,
        style: theme.textTheme.displaySmall?.copyWith(
          fontWeight: FontWeight.bold,
        ),
      ),
      Text(
        'Bấm bài là hát',
        textAlign: TextAlign.center,
        style: theme.textTheme.titleLarge,
      ),
      const SizedBox(height: 32),
      point(Icons.music_note, 'Tự dò tone của bài hát'),
      point(Icons.auto_fix_high, 'Tự chỉnh giọng cho đúng nốt'),
      point(Icons.graphic_eq, 'Tự chọn hiệu ứng theo thể loại'),
      _primary('Bắt đầu', () => _go(_Step.mic)),
      _body(context, 'Mất khoảng 1 phút: cho phép micro, đo máy, thử giọng.'),
    ];
  }

  List<Widget> _micStep(BuildContext context) {
    final theme = Theme.of(context);
    final mic = _mic;
    return [
      _icon(
        context,
        mic == MicStatus.denied ? Icons.mic_off : Icons.mic,
        color: mic == MicStatus.denied ? theme.colorScheme.error : null,
      ),
      _title(context, 'Micro'),
      _body(
        context,
        'ChuẩnTone cần micro để nghe giọng bạn hát, chỉnh giọng cho đúng nốt và thu âm. Âm thanh '
        'không rời khỏi máy (trừ khi bạn tự bấm tách giọng ca sĩ và đồng ý).',
      ),
      if (mic == MicStatus.denied) ...[
        Padding(
          padding: const EdgeInsets.only(top: 16),
          child: Text(
            'Micro đang bị tắt cho ChuẩnTone. Vào Cài đặt của máy → ChuẩnTone → bật Micro.',
            key: const ValueKey('onboarding-mic-denied'),
            textAlign: TextAlign.center,
            style: theme.textTheme.titleMedium?.copyWith(
              color: theme.colorScheme.error,
            ),
          ),
        ),
        _primary('Mở Cài đặt', SystemPermissions.openAppSettings),
        _secondary('Để sau', () => _go(_Step.measure)),
      ] else if (mic == MicStatus.granted)
        _primary('Tiếp', () => _go(_Step.measure))
      else
        _primary(
          _asking ? 'Đang hỏi…' : 'Cho phép dùng micro',
          mic == null || _asking ? null : _askMic,
        ),
    ];
  }

  List<Widget> _measureStep(BuildContext context) {
    final r = _result;
    if (!_measured) {
      final onSpeaker =
          _route == AudioRoute.speaker || _route == AudioRoute.unknown;
      return [
        _icon(context, Icons.speed),
        _title(context, 'Đo nhanh (5 giây)'),
        _body(
          context,
          'Để máy chỗ yên tĩnh, tăng âm lượng khoảng 70%. Máy sẽ kêu xè xè một lúc — số đo '
          'giúp giọng thu khớp nhạc.',
        ),
        if (!onSpeaker)
          _body(context, 'Đang cắm tai nghe — hãy rút ra để đo qua loa.'),
        _primary(
          _measuring ? 'Đang đo…' : 'Đo',
          _measuring ? null : _measure,
          key: const ValueKey('onboarding-measure'),
        ),
        _secondary('Bỏ qua bước này', () => _go(_Step.headphones)),
      ];
    }
    // Đo xong: chỉ hiện kết quả (máy nhỏ vẫn thấy ngay, không bị phần giải thích đẩy xuống).
    if (r == null || !r.valid) {
      return [
        _icon(
          context,
          Icons.hearing_disabled,
          color: Theme.of(context).colorScheme.error,
        ),
        _title(context, 'Chưa đo được'),
        _body(
          context,
          'Máy chưa nghe được tiếng đo. Không sao — đo lại sau ở Cài đặt → Đo độ trễ.',
        ),
        _primary('Tiếp', () => _go(_Step.headphones)),
        _secondary('Đo lại', _measuring ? null : _measure),
      ];
    }
    final v = latencyVerdict(r.latencyMs);
    return [
      _icon(
        context,
        v.level == LatencyLevel.smooth ? Icons.check_circle : Icons.info,
        color: v.level == LatencyLevel.smooth
            ? Colors.green.shade600
            : Colors.orange.shade800,
      ),
      _title(context, v.title),
      _body(context, v.advice),
      _primary('Tiếp', () => _go(_Step.headphones)),
    ];
  }

  List<Widget> _headphones(BuildContext context) {
    final theme = Theme.of(context);
    final (IconData icon, String text, Color color) = switch (_route) {
      AudioRoute.wired || AudioRoute.usb => (
        Icons.headphones,
        'Đã cắm tai nghe có dây: bạn sẽ nghe lại giọng mình khi hát.',
        Colors.green.shade700,
      ),
      AudioRoute.bluetooth => (
        Icons.bluetooth_audio,
        'Tai nghe Bluetooth: chỉ nghe được nhạc, không nghe lại được giọng vì bị trễ.',
        Colors.orange.shade800,
      ),
      _ => (
        Icons.volume_up,
        'Đang dùng loa ngoài: vẫn hát được nhưng không nghe lại giọng.',
        theme.colorScheme.onSurfaceVariant,
      ),
    };
    return [
      _icon(context, Icons.headset),
      _title(context, 'Tai nghe'),
      _body(
        context,
        'Cắm tai nghe có dây (cổng 3,5 mm hoặc adapter Lightning / USB-C) để nghe lại giọng mình '
        'khi hát.',
      ),
      Padding(
        padding: const EdgeInsets.only(top: 20),
        child: Row(
          children: [
            Icon(icon, color: color, size: 32),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                text,
                key: const ValueKey('onboarding-route'),
                style: theme.textTheme.titleMedium?.copyWith(color: color),
              ),
            ),
          ],
        ),
      ),
      _primary('Tiếp', () => _go(_Step.voice)),
    ];
  }

  List<Widget> _voice(BuildContext context) {
    final theme = Theme.of(context);
    final wired = _route == AudioRoute.wired || _route == AudioRoute.usb;
    return [
      _icon(context, Icons.record_voice_over),
      _title(context, 'Thử giọng'),
      _body(
        context,
        wired
            ? 'Nói "a lô" hoặc hát thử: bạn sẽ nghe giọng mình có vang trong tai nghe.'
            : 'Nói "a lô" hoặc hát thử: thanh bên dưới nhảy theo giọng là micro đã nghe được.',
      ),
      const SizedBox(height: 20),
      InputLevelMeter(peakDb: _peakDb),
      const SizedBox(height: 20),
      Text(
        wired ? 'Nghe rõ giọng mình chưa?' : 'Thanh có nhảy theo giọng không?',
        textAlign: TextAlign.center,
        style: theme.textTheme.titleMedium,
      ),
      if (_heard == false) ...[
        const SizedBox(height: 16),
        for (final tip in [
          'Cắm tai nghe có dây — tai nghe Bluetooth không nghe lại được giọng.',
          'Tăng âm lượng máy.',
          'Nói gần micro hơn (cách miệng một gang tay).',
          'Thanh không nhúc nhích: kiểm tra quyền micro trong Cài đặt của máy.',
        ])
          Padding(
            padding: const EdgeInsets.only(bottom: 6),
            child: Text('• $tip', style: theme.textTheme.bodyLarge),
          ),
        _primary('Tiếp', () => _go(_Step.done)),
        _secondary('Mở Cài đặt', SystemPermissions.openAppSettings),
      ] else
        _Action(
          Row(
            children: [
              Expanded(
                child: OutlinedButton(
                  onPressed: () => setState(() => _heard = false),
                  style: OutlinedButton.styleFrom(
                    minimumSize: const Size.fromHeight(56),
                  ),
                  child: const Text('Chưa'),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: FilledButton(
                  onPressed: () => _go(_Step.done),
                  style: FilledButton.styleFrom(
                    minimumSize: const Size.fromHeight(56),
                  ),
                  child: const Text('Rõ rồi'),
                ),
              ),
            ],
          ),
        ),
    ];
  }

  List<Widget> _done(BuildContext context) => [
    _icon(context, Icons.check_circle, color: Colors.green.shade600),
    _title(context, 'Xong! Hát thôi'),
    _body(
      context,
      'Thêm bài từ app Tệp, hoặc mở file nhạc trong Zalo / Safari rồi chọn ChuẩnTone. Chưa có '
      'bài thì hát thử bài mẫu luyện giọng.',
    ),
    _primary('Thêm bài hát đầu tiên', () => _finish(OnboardingExit.addSong)),
    _Action(
      Padding(
        padding: const EdgeInsets.only(top: 8),
        child: OutlinedButton(
          onPressed: () => _finish(OnboardingExit.sample),
          style: OutlinedButton.styleFrom(
            minimumSize: const Size.fromHeight(56),
          ),
          child: const Text('Hát thử bài mẫu'),
        ),
      ),
    ),
    _secondary('Về Trang chủ', () => _finish(OnboardingExit.home)),
  ];
}

/// Đánh dấu nút nằm ở đáy màn hình (xem [_OnboardingPageState._layout]).
class _Action extends StatelessWidget {
  const _Action(this.child);

  final Widget child;

  @override
  Widget build(BuildContext context) => child;
}
