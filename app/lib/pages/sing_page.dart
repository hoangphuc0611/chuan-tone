import 'dart:async';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/app_settings.dart';
import '../data/lrc.dart';
import '../data/song_import.dart';
import '../data/song_ref.dart';
import '../data/take_store.dart';
import '../data/test_log.dart';
import '../engine/labels.dart';
import '../engine/sing_controller.dart';
import '../widgets/analysis_widgets.dart';
import '../widgets/coach_marks.dart';
import '../widgets/correction_chips.dart';
import '../widgets/error_text.dart';
import '../widgets/gain_slider.dart';
import '../widgets/lyrics_view.dart';
import '../widgets/pitch_meter.dart';
import '../widgets/separation_panel.dart';
import '../widgets/soundcard_prompt.dart';
import '../widgets/transpose_control.dart';
import 'review_page.dart';

/// Màn hát (CLAUDE.md mục 5 màn 3; M8 B2, bố cục ở docs/m8-plan.md mục 1): tone thật to, tone nhạc
/// − / + (nhớ theo bài), đồng hồ cao độ, mức chỉnh giọng, chip thể loại, "Vang", "Giữ giọng ca sĩ
/// gốc" (bài đã tách giọng), lời chạy chữ (`.lrc`, B3); thanh phát + nút Thu luôn ở đáy. Nút loa:
/// âm lượng / nghe lại giọng; menu ⋮: lời bài hát, tách giọng, sửa tone. Lần đầu vào: 3 bong bóng
/// mẹo. Cắm thiết bị USB lần đầu: hỏi chế độ soundcard (B6). Logic ở [SingController] (dùng chung
/// với Phòng thử).
class SingPage extends StatefulWidget {
  const SingPage({
    super.key,
    required this.song,
    this.controller,
    this.resumeSeparation = false,
  });

  final SongRef song;

  /// Mở xong thì tiếp tục lần tách giọng dở (Trang chủ → "Tiếp tục", M8 B7).
  final bool resumeSeparation;

  /// Bộ điều khiển phiên hát (test truyền vào; null = tạo mới, huỷ khi rời màn).
  final SingController? controller;

  @override
  State<SingPage> createState() => _SingPageState();
}

enum _Menu { lyrics, separate, editKey }

class _SingPageState extends State<SingPage> with WidgetsBindingObserver {
  late final SingController _c = widget.controller ?? SingController();
  final _toneKey = GlobalKey();
  final _correctionKey = GlobalKey();
  final _recordKey = GlobalKey();
  int _shownMessage = 0;
  bool _tipsChecked = false;
  double? _dragPosition;

  @override
  void initState() {
    super.initState();
    TestLog.screen('sing');
    _c.onTake = _review;
    _c.addListener(_onChange);
    WidgetsBinding.instance.addObserver(this);
    unawaited(
      _c.start(initial: widget.song).then((_) {
        if (widget.resumeSeparation && mounted) {
          unawaited(_c.resumeSeparation());
        }
      }),
    );
  }

  /// Quay lại từ nền: việc bị ngắt (tách giọng) tự tiếp tục.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) _c.onAppResumed();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _c.removeListener(_onChange);
    _c.onTake = null;
    if (widget.controller == null) _c.dispose();
    super.dispose();
  }

  void _onChange() {
    if (!mounted) return;
    final msg = _c.message;
    if (msg != null && _c.messageId != _shownMessage) {
      _shownMessage = _c.messageId;
      showErrorSnack(context, msg, details: _c.messageDetails);
    }
    // Vừa cắm thiết bị USB mà chưa chọn chế độ soundcard → hỏi một lần.
    if (_c.engine != null && _c.service.shouldAskSoundcard) {
      _c.service.markSoundcardAsked();
      unawaited(_askSoundcard());
    }
    // Mẹo chờ dò tone xong: chữ tone đổi kích thước làm lệch chỗ được chỉ.
    if (!_tipsChecked && _c.song != null && !_c.loading && !_c.analyzing) {
      _tipsChecked = true;
      unawaited(_maybeShowTips());
    }
  }

  /// Lần đầu vào Màn hát: bong bóng mẹo chỉ vào Tone, Chỉnh giọng, Thu.
  Future<void> _maybeShowTips() async {
    if (!await _c.settings.firstTimeTip('sing') || !mounted) return;
    // Chờ vẽ xong khung hình có bài để biết vị trí các nút.
    await WidgetsBinding.instance.endOfFrame;
    if (!mounted) return;
    await showCoachMarks(context, [
      CoachMark(
        target: _toneKey,
        title: 'Tone',
        text:
            'Tone của bài hiện ở đây. Hát thấy cao quá hay thấp quá thì bấm − / + để hạ '
            'hoặc nâng tone nhạc. App nhớ tone cho từng bài.',
      ),
      CoachMark(
        target: _correctionKey,
        title: 'Chỉnh giọng',
        text:
            'App tự kéo giọng bạn về đúng nốt theo tone của bài. "Tự nhiên" nhẹ nhàng, '
            '"Ca sĩ" rõ hơn, "Robot" là hiệu ứng giọng máy.',
      ),
      CoachMark(
        target: _recordKey,
        title: 'Thu',
        text:
            'Bấm Thu để thu bài hát. Thu xong bạn nghe lại, chỉnh, rồi xuất video để chia '
            'sẻ.',
      ),
    ]);
  }

  Future<void> _askSoundcard() async {
    final on = await askSoundcardMode(context);
    if (on != null && mounted) await _c.setSoundcardMode(on);
  }

  Future<void> _review(Take take) async {
    if (!mounted) return;
    await Navigator.of(context)
        .push(MaterialPageRoute<void>(builder: (_) => ReviewPage(take: take)));
  }

  Future<void> _editKey() async {
    final c = _c;
    final edit = await showKeyEditor(
      context,
      initial: c.manualKey ?? c.analysis?.key ?? c.service.key,
      canReset: c.manualKey != null,
    );
    if (edit == null) return;
    await c.setManualKey(edit.reset ? null : edit.key);
  }

  void _onMenu(_Menu m) {
    switch (m) {
      case _Menu.lyrics:
        _showLyrics();
      case _Menu.separate:
        _showSeparation();
      case _Menu.editKey:
        unawaited(_editKey());
    }
  }

  /// Chọn file lời cho bài; file không đúng dạng thì báo.
  Future<void> _pickLyrics() async {
    final String? path;
    try {
      path = await pickLyricsFile();
    } catch (_) {
      _snack('Không mở được bộ chọn file.');
      return;
    }
    if (path == null) return;
    try {
      await _c.setLyricsFile(path);
      _snack('Đã nạp lời: ${_c.lyrics!.lines.length} dòng.');
    } on FormatException {
      _snack(
        'File này không phải lời bài hát dạng .lrc (cần có mốc thời gian như [00:12.30]).',
      );
    } catch (_) {
      _snack('Không đọc được file lời.');
    }
  }

  void _snack(String text) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(text)));
  }

  void _showLyrics() {
    unawaited(
      showModalBottomSheet<void>(
        context: context,
        showDragHandle: true,
        isScrollControlled: true,
        builder: (sheet) => ListenableBuilder(
          listenable: _c,
          builder: (sheet, _) => _LyricsSheet(
            c: _c,
            onPick: () {
              Navigator.pop(sheet);
              unawaited(_pickLyrics());
            },
          ),
        ),
      ),
    );
  }

  void _showVolume() {
    unawaited(
      showModalBottomSheet<void>(
        context: context,
        showDragHandle: true,
        isScrollControlled: true,
        builder: (_) => ListenableBuilder(
          listenable: _c,
          builder: (context, _) => _VolumeSheet(c: _c),
        ),
      ),
    );
  }

  void _showSeparation() {
    unawaited(
      showModalBottomSheet<void>(
        context: context,
        showDragHandle: true,
        isScrollControlled: true,
        builder: (sheet) => ListenableBuilder(
          listenable: _c,
          builder: (sheet, _) => SafeArea(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    'Tách giọng ca sĩ',
                    style: Theme.of(sheet).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 8),
                  SeparationPanel(
                    showTitle: false,
                    hasServer: _c.server != null,
                    hasStems: _c.hasStems,
                    progress: _c.sepProgress,
                    error: _c.sepError,
                    errorDetails: _c.sepErrorDetails,
                    hasPending: _c.sepPending != null,
                    onResume: _c.resumeSeparation,
                    onDiscard: _c.discardSeparation,
                    lastResult: _c.sepResult,
                    vocalGain: _c.service.vocalGain,
                    onSeparate: () => _c.separate(
                      askConsent: () => askSeparationConsent(context),
                    ),
                    onCancel: _c.cancelSeparation,
                    onDelete: _c.deleteStems,
                    onVocalGain: _c.setVocalGain,
                    noServerHint: 'Cần địa chỉ máy chủ tách giọng — nhập ở Cài đặt → Nâng cao.',
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: _c,
      builder: (context, _) {
        final c = _c;
        final ready = !c.starting && c.engine != null;
        return Scaffold(
          appBar: AppBar(
            title: Text(
              c.title ?? songTitle(widget.song.name),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            actions: [
              if (ready) ...[
                IconButton(
                  tooltip: 'Âm lượng',
                  icon: const Icon(Icons.volume_up),
                  onPressed: _showVolume,
                ),
                PopupMenuButton<_Menu>(
                  tooltip: 'Thêm',
                  enabled: !c.recording,
                  onSelected: _onMenu,
                  itemBuilder: (_) => [
                    PopupMenuItem(
                      value: _Menu.lyrics,
                      enabled: c.song != null,
                      child: const ListTile(
                        leading: Icon(Icons.lyrics_outlined),
                        title: Text('Lời bài hát'),
                      ),
                    ),
                    PopupMenuItem(
                      value: _Menu.separate,
                      enabled: c.song != null,
                      child: const ListTile(
                        leading: Icon(Icons.record_voice_over),
                        title: Text('Tách giọng ca sĩ'),
                      ),
                    ),
                    PopupMenuItem(
                      value: _Menu.editKey,
                      enabled: c.song != null,
                      child: const ListTile(
                        leading: Icon(Icons.tune),
                        title: Text('Sửa tone của bài'),
                      ),
                    ),
                  ],
                ),
              ],
            ],
          ),
          body: c.starting
              ? const Center(child: CircularProgressIndicator())
              : c.engine == null
              ? _ErrorView(message: c.startError ?? 'Không mở được âm thanh')
              : _body(context),
        );
      },
    );
  }

  Widget _body(BuildContext context) {
    final c = _c;
    final theme = Theme.of(context);
    final pb = c.playback;
    final sep = c.sepProgress;
    return Column(
      children: [
        if (c.loading) const LinearProgressIndicator(),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                KeyedSubtree(
                  key: _toneKey,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      _KeyHeader(c: c),
                      const SizedBox(height: 8),
                      TransposeControl(
                        label: null,
                        value: pb?.transpose ?? 0,
                        audible: pb?.audibleTranspose ?? 0,
                        progress: pb?.transposeProgress ?? 1,
                        onChanged: c.setTranspose,
                        enabled: pb != null && pb.hasBeat && !c.recording,
                      ),
                      if (c.recording)
                        Text(
                          'Đang thu: tone được giữ nguyên.',
                          textAlign: TextAlign.center,
                          style: theme.textTheme.bodySmall,
                        ),
                    ],
                  ),
                ),
                const SizedBox(height: 8),
                ValueListenableBuilder(
                  valueListenable: c.pitch,
                  builder: (context, pitch, _) =>
                      PitchMeter(state: pitch, compact: true),
                ),
                if (c.lyrics case final lyrics?)
                  Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: LyricsView(
                      lyrics: lyrics,
                      offsetMs: c.lyricsOffsetMs,
                      position: c.position,
                    ),
                  ),
                const SizedBox(height: 12),
                KeyedSubtree(
                  key: _correctionKey,
                  child: _CorrectionPicker(c: c),
                ),
                const SizedBox(height: 16),
                if (c.service.usingSoundcardFx)
                  _SoundcardFx(c: c)
                else ...[
                  _GenreChips(c: c),
                  GainSlider(
                    label: 'Vang',
                    value: c.service.fxMix,
                    max: 2,
                    onChanged: c.service.genre == Genre.dry ? null : c.setFxMix,
                  ),
                ],
                if (c.hasStems)
                  GainSlider(
                    label: 'Giữ giọng ca sĩ gốc',
                    value: c.service.vocalGain,
                    onChanged: c.setVocalGain,
                  ),
                if (sep == null && c.sepPending != null)
                  Card(
                    key: const ValueKey('sing-sep-pending'),
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(12, 12, 4, 4),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          const Text('Tách giọng ca sĩ chưa xong.'),
                          Wrap(
                            alignment: WrapAlignment.end,
                            children: [
                              TextButton(
                                onPressed: c.discardSeparation,
                                child: const Text('Bỏ'),
                              ),
                              TextButton(
                                onPressed: c.resumeSeparation,
                                child: const Text('Tiếp tục tách'),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
                if (sep != null)
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(12, 12, 4, 4),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          Text(separationStageLabel(sep)),
                          const SizedBox(height: 6),
                          LinearProgressIndicator(value: sep.fraction),
                          Align(
                            alignment: Alignment.centerRight,
                            child: TextButton(
                              onPressed: c.cancelSeparation,
                              child: const Text('Huỷ tách giọng'),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                if (c.cameraOn) ...[
                  const SizedBox(height: 8),
                  Center(
                    child: ClipRRect(
                      borderRadius: BorderRadius.circular(12),
                      child: const SizedBox(
                        height: 200,
                        child: AspectRatio(
                          aspectRatio: 9 / 16,
                          child: CameraPreviewView(),
                        ),
                      ),
                    ),
                  ),
                ],
              ],
            ),
          ),
        ),
        _TransportBar(
          c: c,
          recordKey: _recordKey,
          dragPosition: _dragPosition,
          onDrag: (v) => setState(() => _dragPosition = v),
          onDragEnd: (v) {
            c.seek(Duration(milliseconds: v.round()));
            setState(() => _dragPosition = null);
          },
        ),
      ],
    );
  }
}

/// Tone đang nghe (chữ to nhất màn) + tone lấy từ đâu + mức tone nhạc.
class _KeyHeader extends StatelessWidget {
  const _KeyHeader({required this.c});

  final SingController c;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final a = c.analysis;
    final opening =
        c.loading; // đang nạp bài: tone của bài trước / bản thu chưa đổi
    final detecting = c.analyzing && a == null && c.manualKey == null;
    final source = switch (c.keySource) {
      KeySource.song => 'theo bài',
      KeySource.manual => 'tone bạn đã sửa',
      KeySource.picked =>
        a != null && a.key == null
            ? 'chưa dò được tone — bấm ⋮ để sửa'
            : 'chọn tay',
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          opening
              ? 'Đang mở bài…'
              : detecting
              ? 'Đang dò tone…'
              : keyLabel(c.currentKey),
          key: const ValueKey('sing-key'),
          textAlign: TextAlign.center,
          style:
              (opening || detecting
                      ? theme.textTheme.headlineSmall
                      : theme.textTheme.displaySmall)
                  ?.copyWith(fontWeight: FontWeight.w700),
        ),
        if (!opening && !detecting)
          Text(
            source,
            textAlign: TextAlign.center,
            style: theme.textTheme.bodyLarge,
          ),
        if (c.analyzing)
          Padding(
            padding: const EdgeInsets.fromLTRB(48, 8, 48, 0),
            child: LinearProgressIndicator(value: c.analysisStatus?.progress),
          ),
        if (c.analysisError != null)
          Text(
            c.analysisError!,
            textAlign: TextAlign.center,
            style: TextStyle(color: theme.colorScheme.error),
          ),
      ],
    );
  }
}

/// Mức chỉnh giọng: Tắt / Tự nhiên / Ca sĩ / Robot (xuống dòng khi màn hẹp, chữ to).
class _CorrectionPicker extends StatelessWidget {
  const _CorrectionPicker({required this.c});

  final SingController c;

  @override
  Widget build(BuildContext context) {
    final current = c.service.correction;
    return CorrectionChips(
      value: current,
      onChanged: c.setCorrection,
      hint: current != null && !c.monitorOn
          ? (c.service.soundcardActive
                ? 'Chế độ soundcard: giọng đã chỉnh có trong bản thu và khi nghe lại.'
                : 'Bật "Nghe lại giọng" ở nút loa (trên cùng) để nghe giọng đã chỉnh trong tai nghe.')
          : null,
    );
  }
}

/// Chip thể loại cuộn ngang + thể loại đang chọn có từ đâu.
class _GenreChips extends StatelessWidget {
  const _GenreChips({required this.c});

  final SingController c;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final source = c.genreSource;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Thể loại', style: theme.textTheme.titleMedium),
        const SizedBox(height: 6),
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: Row(
            children: [
              for (final g in Genre.values)
                Padding(
                  padding: const EdgeInsets.only(right: 8),
                  child: ChoiceChip(
                    label: Text(genreLabel(g)),
                    labelStyle: theme.textTheme.titleMedium,
                    selected: g == c.service.genre,
                    showCheckmark: false,
                    onSelected: (_) => c.pickGenre(g),
                  ),
                ),
            ],
          ),
        ),
        if (source != null)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              genreSourceLabel(source, bpm: c.analysis?.bpm),
              style: theme.textTheme.bodySmall,
            ),
          ),
        if (c.service.soundcardActive) ...[
          const SizedBox(height: 4),
          Text(soundcardFxHelp.app, style: theme.textTheme.bodySmall),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton(
              onPressed: () => c.setSoundcardFx(SoundcardFx.soundcard),
              child: const Text('Dùng vang của soundcard'),
            ),
          ),
        ],
      ],
    );
  }
}

/// Chế độ soundcard, giữ vang của soundcard: thay chip thể loại (bản thu để Khô, M8 B6).
class _SoundcardFx extends StatelessWidget {
  const _SoundcardFx({required this.c});

  final SingController c;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      key: const ValueKey('sing-soundcard-fx'),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 12, 4, 4),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(
                  Icons.settings_input_component,
                  color: theme.colorScheme.primary,
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    'Đang dùng vang của soundcard',
                    style: theme.textTheme.titleMedium,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 4),
            Padding(
              padding: const EdgeInsets.only(right: 8),
              child: Text(soundcardFxHelp.soundcard),
            ),
            Align(
              alignment: Alignment.centerRight,
              child: TextButton(
                onPressed: () => c.setSoundcardFx(SoundcardFx.app),
                child: const Text('Dùng hiệu ứng của app'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Đáy màn: cảnh báo đường tiếng, thanh vị trí, Lùi 10 giây, Phát / Dừng, Thu, camera.
class _TransportBar extends StatelessWidget {
  const _TransportBar({
    required this.c,
    required this.recordKey,
    required this.dragPosition,
    required this.onDrag,
    required this.onDragEnd,
  });

  final SingController c;
  final GlobalKey recordKey;
  final double? dragPosition;
  final ValueChanged<double> onDrag;
  final ValueChanged<double> onDragEnd;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final pb = c.playback;
    final hasBeat = pb != null && pb.hasBeat;
    // Đang nạp bài: chưa hiện vị trí / độ dài của bài trước hay bản thu vừa nghe. Không đổi trạng
    // thái bật / tắt của thanh trượt ở đây (đổi giữa lúc chuyển màn làm hỏng cây semantics).
    final duration = c.loading ? Duration.zero : pb?.duration ?? Duration.zero;
    final position = c.loading ? Duration.zero : pb?.position ?? Duration.zero;
    final maxMs = duration.inMilliseconds.toDouble();
    final route = c.metrics?.route;
    final warning = route == AudioRoute.bluetooth
        ? 'Tai nghe Bluetooth: không nghe lại được giọng, bản thu có thể lệch nhạc. '
              'Nên dùng tai nghe có dây.'
        : c.monitorOn && route == AudioRoute.speaker
        ? 'Đang dùng loa ngoài: dễ bị hú. Hãy cắm tai nghe có dây.'
        : null;
    // Chế độ soundcard, lúc đếm ngược / vài giây đầu khi thu: nhắc tắt Loopback (nhạc trộn vào micro
    // thì bản thu lẫn nhạc, B6).
    final loopbackTip =
        c.service.soundcardActive &&
        (c.countdown != null ||
            (c.recording &&
                (c.recStatus?.duration ?? Duration.zero) <
                    const Duration(seconds: 5)));
    final playing = pb?.playing ?? false;
    final red = theme.colorScheme.error;
    // Nút thu luôn đỏ chữ trắng (màu "lỗi" của giao diện tối là hồng nhạt).
    final recordStyle = FilledButton.styleFrom(
      backgroundColor: const Color(0xFFD32F2F),
      foregroundColor: Colors.white,
      minimumSize: const Size.fromHeight(56),
      textStyle: theme.textTheme.titleMedium,
    );
    final countdown = c.countdown;
    return Material(
      color: theme.colorScheme.surfaceContainer,
      elevation: 3,
      child: SafeArea(
        top: false,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(12, 4, 12, 8),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (warning != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(4, 4, 4, 0),
                  child: Text(
                    warning,
                    style: theme.textTheme.bodySmall?.copyWith(color: red),
                  ),
                ),
              if (loopbackTip)
                Padding(
                  padding: const EdgeInsets.fromLTRB(4, 4, 4, 0),
                  child: Text(
                    'Chế độ soundcard: nhớ tắt Loopback để giọng không lẫn nhạc.',
                    key: const ValueKey('sing-loopback-tip'),
                    style: theme.textTheme.bodyMedium?.copyWith(
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              Slider(
                value: (dragPosition ?? position.inMilliseconds.toDouble())
                    .clamp(0, maxMs > 0 ? maxMs : 1),
                max: maxMs > 0 ? maxMs : 1,
                onChanged: hasBeat && !c.recording ? onDrag : null,
                onChangeEnd: hasBeat && !c.recording ? onDragEnd : null,
              ),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      formatDuration(
                        dragPosition == null
                            ? position
                            : Duration(milliseconds: dragPosition!.round()),
                      ),
                    ),
                    Text(formatDuration(duration)),
                  ],
                ),
              ),
              const SizedBox(height: 4),
              Row(
                children: [
                  IconButton(
                    tooltip: 'Lùi 10 giây',
                    iconSize: 32,
                    onPressed: hasBeat && !c.recording
                        ? () => c.skip(const Duration(seconds: -10))
                        : null,
                    icon: const Icon(Icons.replay_10),
                  ),
                  IconButton.filled(
                    tooltip: playing ? 'Tạm dừng' : 'Phát',
                    iconSize: 36,
                    onPressed: hasBeat && !c.recording ? c.playPause : null,
                    icon: Icon(playing ? Icons.pause : Icons.play_arrow),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: KeyedSubtree(
                      key: recordKey,
                      child: c.recording
                          ? FilledButton.icon(
                              onPressed: c.stopRecording,
                              icon: const Icon(Icons.stop),
                              label: FittedBox(
                                fit: BoxFit.scaleDown,
                                child: Text(
                                  'Dừng · ${formatDuration(c.recStatus?.duration ?? Duration.zero)}',
                                ),
                              ),
                              style: recordStyle,
                            )
                          : FilledButton.icon(
                              onPressed: c.canRecord ? c.startRecording : null,
                              icon: const Icon(Icons.fiber_manual_record),
                              label: FittedBox(
                                fit: BoxFit.scaleDown,
                                child: Text(
                                  countdown != null
                                      ? 'Chuẩn bị… $countdown'
                                      : 'Thu',
                                ),
                              ),
                              style: recordStyle,
                            ),
                    ),
                  ),
                  if (c.camera.supported)
                    IconButton(
                      tooltip: c.cameraOn ? 'Tắt quay video' : 'Quay video',
                      iconSize: 28,
                      isSelected: c.cameraOn,
                      onPressed: c.recording
                          ? null
                          : () => c.setCamera(!c.cameraOn),
                      icon: const Icon(Icons.videocam_off_outlined),
                      selectedIcon: const Icon(Icons.videocam),
                    ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Lời bài hát: nạp / đổi file `.lrc`, lời sớm / muộn 0,5 s, bỏ lời.
class _LyricsSheet extends StatelessWidget {
  const _LyricsSheet({required this.c, required this.onPick});

  final SingController c;
  final VoidCallback onPick;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final lyrics = c.lyrics;
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('Lời bài hát', style: theme.textTheme.titleLarge),
            const SizedBox(height: 4),
            Text(
              lyrics == null
                  ? 'Chọn file lời .lrc của bài (có mốc thời gian từng câu). App không kèm sẵn '
                        'lời bài nào.'
                  : 'Đã có lời: ${lyrics.lines.length} dòng.',
            ),
            const SizedBox(height: 12),
            FilledButton.tonalIcon(
              onPressed: onPick,
              icon: const Icon(Icons.file_open_outlined),
              label: Text(lyrics == null ? 'Chọn file lời' : 'Đổi file lời'),
              style: FilledButton.styleFrom(
                minimumSize: const Size.fromHeight(52),
              ),
            ),
            if (lyrics != null) ...[
              const SizedBox(height: 16),
              Text(
                lyricsOffsetLabel(c.lyricsOffsetMs),
                key: const ValueKey('lyrics-offset'),
                textAlign: TextAlign.center,
                style: theme.textTheme.titleMedium,
              ),
              const SizedBox(height: 8),
              Wrap(
                alignment: WrapAlignment.center,
                spacing: 8,
                runSpacing: 8,
                children: [
                  OutlinedButton(
                    onPressed: () => c.nudgeLyrics(-500),
                    child: const Text('Lời muộn hơn'),
                  ),
                  OutlinedButton(
                    onPressed: () => c.nudgeLyrics(500),
                    child: const Text('Lời sớm hơn'),
                  ),
                ],
              ),
              const SizedBox(height: 4),
              Text(
                'Mỗi lần bấm dời lời 0,5 giây.',
                textAlign: TextAlign.center,
                style: theme.textTheme.bodySmall,
              ),
              Align(
                alignment: Alignment.centerRight,
                child: TextButton(
                  onPressed: c.removeLyrics,
                  child: const Text('Bỏ lời'),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Âm lượng nhạc / giọng, nghe lại giọng, lọc tiếng ồn micro.
class _VolumeSheet extends StatelessWidget {
  const _VolumeSheet({required this.c});

  final SingController c;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final speaker = c.monitorOn && c.metrics?.route == AudioRoute.speaker;
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('Âm lượng', style: theme.textTheme.titleLarge),
            const SizedBox(height: 8),
            GainSlider(
              label: 'Nhạc',
              value: c.beatGain,
              onChanged: c.setBeatGain,
            ),
            GainSlider(
              label: 'Giọng',
              value: c.monitorGain,
              max: 2,
              onChanged: c.setMonitorGain,
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Nghe lại giọng'),
              subtitle: Text(
                c.service.soundcardActive
                    ? 'Đang dùng soundcard: soundcard đã cho nghe giọng trực tiếp'
                    : 'Nghe giọng mình qua tai nghe khi hát',
              ),
              value: c.monitorOn,
              onChanged: c.setMonitorOn,
            ),
            if (speaker)
              Text(
                'Đang dùng loa ngoài: dễ bị hú. Hãy cắm tai nghe có dây.',
                style: TextStyle(color: theme.colorScheme.error),
              ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Lọc tiếng ồn micro'),
              subtitle: const Text('Giảm tiếng ồn nền lúc không hát'),
              value: c.service.gateOn,
              onChanged: c.setGate,
            ),
          ],
        ),
      ),
    );
  }
}

class _ErrorView extends StatelessWidget {
  const _ErrorView({required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: SingleChildScrollView(
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
