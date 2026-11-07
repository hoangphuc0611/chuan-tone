import 'dart:async';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';

import 'data/sample_songs.dart';
import 'data/song_import.dart';
import 'data/song_library.dart';
import 'data/stem_store.dart';
import 'data/test_log.dart';
import 'data/take_store.dart';
import 'engine/labels.dart';
import 'engine_info.dart';
import 'data/app_settings.dart';
import 'pages/analysis_page.dart';
import 'pages/onboarding_page.dart';
import 'pages/settings_page.dart';
import 'pages/sing_page.dart';
import 'pages/songs_page.dart';
import 'pages/takes_page.dart';
import 'theme.dart';
import 'widgets/app_ui.dart';

/// Báo cho Trang chủ khi quay lại từ màn khác (để cập nhật "Hát tiếp", số bản thu…).
final homeRouteObserver = RouteObserver<ModalRoute<void>>();

class ChuanToneApp extends StatefulWidget {
  const ChuanToneApp({super.key, required this.engineInfo, this.library});

  final EngineInfo engineInfo;

  /// Thư viện bài (null = thư viện của app; test truyền thư viện trong thư mục tạm).
  final SongLibrary? library;

  @override
  State<ChuanToneApp> createState() => _ChuanToneAppState();
}

class _ChuanToneAppState extends State<ChuanToneApp> {
  final _navigator = GlobalKey<NavigatorState>();
  final _messenger = GlobalKey<ScaffoldMessengerState>();
  late final SongLibrary _library = widget.library ?? SongLibrary();

  @override
  void initState() {
    super.initState();
    IncomingFiles.listen(_onIncoming);
  }

  void _snack(String text) => _messenger.currentState
    ?..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(text)));

  /// "Mở bằng ChuẩnTone": thêm các file nhạc vào thư viện (kèm file lời gửi cùng lúc, ghép theo
  /// tên), rồi mở màn Phân tích bài của bài cuối. Chỉ có file lời: gắn cho bài cùng tên trong thư
  /// viện nếu có.
  Future<void> _onIncoming(List<IncomingFile> files) async {
    final audio = [
      for (final f in files)
        if (!isLyricsFile(f.name)) f,
    ];
    final lyrics = [
      for (final f in files)
        if (isLyricsFile(f.name)) f,
    ];
    final pairs = pairLyrics(
      [for (final a in audio) a.name],
      [for (final l in lyrics) l.name],
    );
    LibrarySong? last;
    var failed = 0;
    for (final f in audio) {
      try {
        last = await _library.add(f.path, name: f.name, move: true);
        final lrc = pairs[f.name];
        if (lrc != null) {
          try {
            await _library.setLyrics(
              last.name,
              last.size,
              lyrics.firstWhere((l) => l.name == lrc).path,
            );
          } on Object {
            // file lời hỏng: bài vẫn thêm được
          }
        }
      } catch (_) {
        failed++;
      }
    }
    if (audio.isEmpty && lyrics.isNotEmpty) {
      await _attachLonelyLyrics(lyrics.first);
      return;
    }
    if (failed > 0) {
      _snack(
        failed == 1
            ? 'Không thêm được file nhạc vừa mở.'
            : 'Không thêm được $failed file nhạc vừa mở.',
      );
    }
    final song = last;
    if (song == null || !widget.engineInfo.ok) return;
    TestLog.event('song.add', {'source': 'incoming', 'lyrics': song.hasLyrics});
    await _navigator.currentState?.push(
      MaterialPageRoute<void>(builder: (_) => AnalysisPage(song: song.ref)),
    );
  }

  /// File lời gửi riêng: gắn cho bài trong thư viện có cùng tên (bỏ đuôi, bỏ dấu).
  Future<void> _attachLonelyLyrics(IncomingFile f) async {
    final songs = await _library.songs();
    final match = [
      for (final s in songs)
        if (sameBaseName(s.name, f.name) ||
            sameBaseName('${s.title}.lrc', f.name))
          s,
    ];
    if (match.length != 1) {
      _snack(
        'Đã nhận file lời. Mở bài hát rồi vào ⋮ → Lời bài hát để gắn lời cho bài.',
      );
      return;
    }
    try {
      await _library.setLyrics(match.single.name, match.single.size, f.path);
      _snack('Đã gắn lời cho bài "${match.single.title}".');
    } on FormatException {
      _snack('File này không phải lời bài hát dạng .lrc.');
    } catch (_) {
      _snack('Không đọc được file lời.');
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ChuẩnTone',
      debugShowCheckedModeBanner: false,
      navigatorKey: _navigator,
      scaffoldMessengerKey: _messenger,
      navigatorObservers: [homeRouteObserver],
      theme: appTheme(Brightness.light),
      darkTheme: appTheme(Brightness.dark),
      locale: appLocale,
      supportedLocales: const [appLocale],
      localizationsDelegates: appLocalizationsDelegates,
      home: _StartGate(engineInfo: widget.engineInfo, library: _library),
    );
  }
}

/// Mở app: lần đầu (chưa xong hướng dẫn, engine chạy được) → hướng dẫn; xong thì Trang chủ, làm
/// luôn việc người dùng chọn ở bước cuối.
class _StartGate extends StatefulWidget {
  const _StartGate({required this.engineInfo, required this.library});

  final EngineInfo engineInfo;
  final SongLibrary library;

  @override
  State<_StartGate> createState() => _StartGateState();
}

class _StartGateState extends State<_StartGate> {
  bool? _onboarding;
  OnboardingExit? _action;

  @override
  void initState() {
    super.initState();
    if (!widget.engineInfo.ok) {
      _onboarding = false; // engine lỗi: Trang chủ báo lỗi
    } else {
      unawaited(_load());
    }
  }

  Future<void> _load() async {
    var done = true;
    try {
      done = await AppSettings(baseDir: widget.library.baseDir)
          .onboardingDone();
    } catch (_) {}
    if (mounted) setState(() => _onboarding = !done);
  }

  @override
  Widget build(BuildContext context) {
    return switch (_onboarding) {
      null => const Scaffold(),
      true => OnboardingPage(
        settings: AppSettings(baseDir: widget.library.baseDir),
        onFinish: (exit) => setState(() {
          _onboarding = false;
          _action = exit;
        }),
      ),
      false => MainShell(
        engineInfo: widget.engineInfo,
        library: widget.library,
        initialAction: _action,
      ),
    };
  }
}

/// Khung 4 tab: Trang chủ / Bài hát / Bản thu / Cài đặt (docs/ui/redesign-plan.md). Màn hát, Nghe lại…
/// mở đè lên khung nên không còn tab khi hát. Chọn tab là dựng lại màn đó (đọc lại danh sách mới).
class MainShell extends StatefulWidget {
  const MainShell({
    super.key,
    required this.engineInfo,
    required this.library,
    this.initialAction,
  });

  final EngineInfo engineInfo;
  final SongLibrary library;

  /// Việc chọn ở bước cuối của hướng dẫn lần đầu (chỉ làm một lần).
  final OnboardingExit? initialAction;

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  int _tab = 0;
  late OnboardingExit? _action = widget.initialAction;

  void _go(int tab) => setState(() => _tab = tab);

  @override
  Widget build(BuildContext context) {
    final action = _action;
    _action = null; // quay lại Trang chủ từ tab khác không làm lại
    final baseDir = widget.library.baseDir;
    return Scaffold(
      body: switch (_tab) {
        1 => SongsPage(library: widget.library),
        2 => TakesPage(store: TakeStore(baseDir: baseDir)),
        3 => SettingsPage(
          engineInfo: widget.engineInfo,
          library: widget.library,
        ),
        _ => HomePage(
          engineInfo: widget.engineInfo,
          library: widget.library,
          initialAction: action,
          onSongs: () => _go(1),
          onTakes: () => _go(2),
        ),
      },
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: _go,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.home_outlined),
            selectedIcon: Icon(Icons.home_rounded),
            label: 'Trang chủ',
          ),
          NavigationDestination(
            icon: Icon(Icons.queue_music_rounded),
            label: 'Bài hát',
          ),
          NavigationDestination(
            icon: Icon(Icons.graphic_eq_rounded),
            label: 'Bản thu',
          ),
          NavigationDestination(
            icon: Icon(Icons.settings_outlined),
            selectedIcon: Icon(Icons.settings_rounded),
            label: 'Cài đặt',
          ),
        ],
      ),
    );
  }
}

/// Trang chủ (M8 B3, giao diện mới ở docs/ui/redesign-plan.md): thẻ lớn "Thêm bài hát", "Hát tiếp"
/// (bài mới dùng — bấm là vào Màn hát với tone / thể loại / lời như lần trước), "Tất cả bài hát", bài
/// mẫu, bản thu. Engine lỗi thì báo ngay đầu trang. Có bài đang tách giọng dở (bị ngắt) thì nhắc
/// "Tiếp tục" (M8 B7).
class HomePage extends StatefulWidget {
  const HomePage({
    super.key,
    required this.engineInfo,
    required this.library,
    this.takes,
    this.onSing,
    this.initialAction,
    this.onSongs,
    this.onTakes,
  });

  final EngineInfo engineInfo;
  final SongLibrary library;

  /// Việc chọn ở bước cuối của hướng dẫn lần đầu (thêm bài / hát bài mẫu).
  final OnboardingExit? initialAction;

  /// Kho bản thu (null = cùng thư mục dữ liệu với [library]).
  final TakeStore? takes;

  /// Mở Màn hát cho bài (mặc định [SingPage]; [resume] = tiếp tục tách giọng dở); test thay bằng
  /// hàm giả.
  final Future<void> Function(
    BuildContext context,
    LibrarySong song, {
    bool resume,
  })?
  onSing;

  /// Chuyển sang tab Bài hát / Bản thu (null = mở màn riêng).
  final VoidCallback? onSongs;
  final VoidCallback? onTakes;

  /// Số bài ở "Hát tiếp".
  static const recentCount = 3;

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with RouteAware {
  List<LibrarySong>? _songs;
  int _takeCount = 0;
  String? _installing; // id bài mẫu đang chép vào thư viện
  List<LibrarySong> _pendingSep = const []; // bài đang tách giọng dở

  late final TakeStore _takes =
      widget.takes ?? TakeStore(baseDir: widget.library.baseDir);

  bool get _ok => widget.engineInfo.ok;

  @override
  void initState() {
    super.initState();
    TestLog.screen('home');
    unawaited(_reload());
    final action = widget.initialAction;
    if (action != null && action != OnboardingExit.home) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted) return;
        if (action == OnboardingExit.addSong) {
          unawaited(_showAddSheet());
        } else {
          unawaited(_openSample(sampleSongs.first));
        }
      });
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final route = ModalRoute.of(context);
    if (route != null) homeRouteObserver.subscribe(this, route);
  }

  @override
  void dispose() {
    homeRouteObserver.unsubscribe(this);
    super.dispose();
  }

  @override
  void didPopNext() {
    TestLog.screen('home');
    unawaited(_reload());
  }

  Future<void> _reload() async {
    try {
      final songs = await widget.library.songs(
        engineVersion: _ok ? widget.engineInfo.version : null,
      );
      final takes = await _takes.list();
      final pending = <LibrarySong>[];
      for (final p in await StemStore(
        baseDir: widget.library.baseDir,
      ).pending()) {
        final s = await widget.library.find(p.name, p.size);
        if (s != null && !s.missing) pending.add(s);
      }
      if (!mounted) return;
      setState(() {
        _songs = songs;
        _takeCount = takes.length;
        _pendingSep = pending;
      });
    } catch (_) {
      if (mounted) setState(() => _songs = const []);
    }
  }

  void _snack(String text) {
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(text)));
  }

  Future<void> _push(Widget page) =>
      Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => page));

  Future<void> _sing(LibrarySong song, {bool resume = false}) async {
    if (song.missing) {
      _snack(
        'File nhạc của bài này không còn trên máy. Hãy xoá rồi thêm lại bài.',
      );
      return;
    }
    final sing = widget.onSing;
    if (sing != null) {
      await sing(context, song, resume: resume);
    } else {
      await _push(SingPage(song: song.ref, resumeSeparation: resume));
    }
  }

  /// Bảng "Thêm bài hát": chọn từ Tệp, hoặc cách gửi từ Zalo (ba bước).
  Future<void> _showAddSheet() async {
    final pick = await showModalBottomSheet<bool>(
      context: context,
      isScrollControlled: true,
      builder: (_) => const _AddSongSheet(),
    );
    if (pick == true && mounted) await _addSong();
  }

  /// Chọn file (app Tệp trên iOS, không cần quyền thư viện nhạc; chọn kèm file lời được) → thư
  /// viện bài → màn Phân tích bài.
  Future<void> _addSong() async {
    final LibrarySong? song;
    try {
      song = await pickSongIntoLibrary(widget.library);
    } catch (_) {
      _snack('Không thêm được file này. Hãy thử file MP3, M4A hoặc WAV.');
      return;
    }
    if (song == null || !mounted) return;
    TestLog.event('song.add', {'source': 'pick', 'lyrics': song.hasLyrics});
    await _push(AnalysisPage(song: song.ref));
  }

  Future<void> _openSample(SampleSong s) async {
    if (_installing != null) return;
    setState(() => _installing = s.id);
    LibrarySong? song;
    try {
      song = await installSample(widget.library, s);
    } catch (_) {
      _snack('Không mở được bài mẫu.');
    }
    if (!mounted) return;
    setState(() => _installing = null);
    if (song != null) {
      TestLog.event('song.add', {'source': 'sample'});
      await _sing(song);
    }
    unawaited(_reload());
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    final songs = _songs;
    final recent = songs == null
        ? const <LibrarySong>[]
        : songs.take(HomePage.recentCount).toList();
    return Scaffold(
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
          children: [
            Row(
              children: [
                IconBadge(
                  Icons.mic_rounded,
                  size: 44,
                  background: cs.primary,
                  foreground: cs.onPrimary,
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'ChuẩnTone',
                        style: theme.textTheme.headlineSmall?.copyWith(
                          color: cs.primary,
                        ),
                      ),
                      Text(
                        'Bấm bài là hát',
                        style: theme.textTheme.bodyMedium?.copyWith(
                          color: cs.onSurfaceVariant,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            if (!_ok) ...[
              const SizedBox(height: 12),
              EngineStatusCard(info: widget.engineInfo),
            ],
            const SizedBox(height: 16),
            _AddSongHero(onTap: _ok ? _showAddSheet : null),
            for (final s in _pendingSep)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Card(
                  key: ValueKey('home-sep-pending-${s.name}'),
                  color: cs.secondaryContainer,
                  clipBehavior: Clip.antiAlias,
                  child: ListTile(
                    textColor: cs.onSecondaryContainer,
                    iconColor: cs.onSecondaryContainer,
                    leading: const Icon(Icons.hourglass_top_rounded),
                    title: Text('Đang tách giọng dở: ${s.title}'),
                    subtitle: const Text(
                      'Bị ngắt giữa chừng — bấm để tách tiếp',
                    ),
                    trailing: const Icon(Icons.play_arrow_rounded),
                    enabled: _ok,
                    onTap: () => _sing(s, resume: true),
                  ),
                ),
              ),
            if (recent.isNotEmpty) ...[
              const SectionHeader('Hát tiếp'),
              Card(
                clipBehavior: Clip.antiAlias,
                child: Column(
                  children: [
                    for (final (i, s) in recent.indexed)
                      _SongRow(
                        title: s.title,
                        subtitle: _recentSubtitle(s),
                        missing: s.missing,
                        tint: i,
                        enabled: _ok,
                        onTap: () => _sing(s),
                      ),
                    ListTile(
                      title: Text(
                        'Tất cả bài hát (${songs!.length})',
                        style: theme.textTheme.titleSmall?.copyWith(
                          color: cs.primary,
                        ),
                      ),
                      trailing: Icon(Icons.chevron_right, color: cs.primary),
                      enabled: _ok,
                      onTap:
                          widget.onSongs ??
                          () => _push(SongsPage(library: widget.library)),
                    ),
                  ],
                ),
              ),
            ] else if (songs != null) ...[
              const SizedBox(height: 16),
              const EmptyState(
                icon: Icons.queue_music_rounded,
                title: 'Chưa có bài nào',
                body:
                    'Mở file nhạc trong Zalo hoặc Tệp rồi chọn "Mở bằng ChuẩnTone" — '
                    'hoặc hát thử bài mẫu bên dưới.',
              ),
            ],
            const SectionHeader('Bài mẫu'),
            IntrinsicHeight(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  for (final (i, s) in sampleSongs.indexed) ...[
                    if (i > 0) const SizedBox(width: 8),
                    Expanded(
                      child: _SampleCard(
                        title: s.title,
                        subtitle: s.subtitle,
                        tint: i,
                        busy: _installing == s.id,
                        enabled: _ok,
                        onTap: () => _openSample(s),
                      ),
                    ),
                  ],
                ],
              ),
            ),
            const SizedBox(height: 16),
            Card(
              color: cs.surfaceContainer,
              clipBehavior: Clip.antiAlias,
              child: ListTile(
                contentPadding: const EdgeInsets.symmetric(
                  horizontal: 16,
                  vertical: 4,
                ),
                leading: IconBadge(
                  Icons.graphic_eq_rounded,
                  size: 44,
                  background: cs.primary,
                  foreground: cs.onPrimary,
                ),
                title: Text(
                  _takeCount == 0
                      ? 'Bản thu của tôi'
                      : 'Bản thu của tôi ($_takeCount)',
                  style: theme.textTheme.titleMedium,
                ),
                trailing: const Icon(Icons.chevron_right),
                enabled: _ok,
                onTap: widget.onTakes ?? () => _push(TakesPage(store: _takes)),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// "Sol thứ (Gm) · tone +2 · có lời".
String _recentSubtitle(LibrarySong s) {
  if (s.missing) return 'File nhạc không còn trên máy';
  final k = s.key0;
  return [
    k == null ? 'Chưa dò tone' : keyLabel(k),
    if (s.transpose != 0) 'tone ${transposeLabel(s.transpose)}',
    if (s.hasLyrics) 'có lời',
  ].join(' · ');
}

/// Thẻ lớn nhất của Trang chủ: thêm bài.
class _AddSongHero extends StatelessWidget {
  const _AddSongHero({required this.onTap});

  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return HeroCard(
      onTap: onTap,
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Thêm bài hát',
                  style: theme.textTheme.headlineSmall?.copyWith(
                    color: Colors.white,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  'Từ Tệp, Zalo, Safari…',
                  style: theme.textTheme.bodyLarge?.copyWith(
                    color: Colors.white,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 12),
          const IconBadge(
            Icons.add_rounded,
            size: 56,
            background: Colors.white,
            foreground: brandColor,
          ),
        ],
      ),
    );
  }
}

/// Màu vòng tròn đầu dòng, xoay vòng cho khỏi đơn điệu.
(Color, Color) _tintColors(ColorScheme cs, int i) => switch (i % 3) {
  0 => (cs.primaryContainer, cs.onPrimaryContainer),
  1 => (cs.secondaryContainer, cs.onSecondaryContainer),
  _ => (cs.surfaceContainerHigh, cs.onSurfaceVariant),
};

/// Một bài ở "Hát tiếp": bấm cả dòng là vào hát.
class _SongRow extends StatelessWidget {
  const _SongRow({
    required this.title,
    required this.subtitle,
    required this.enabled,
    required this.onTap,
    this.tint = 0,
    this.missing = false,
  });

  final String title;
  final String subtitle;
  final bool enabled;
  final VoidCallback onTap;
  final int tint;
  final bool missing;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    final (bg, fg) = _tintColors(cs, tint);
    return ListTile(
      contentPadding: const EdgeInsets.symmetric(horizontal: 12),
      minVerticalPadding: 10,
      leading: IconBadge(
        missing ? Icons.music_off_rounded : Icons.music_note_rounded,
        background: missing ? cs.errorContainer : bg,
        foreground: missing ? cs.onErrorContainer : fg,
      ),
      title: Text(
        title,
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
        style: theme.textTheme.titleMedium,
      ),
      subtitle: Text(
        subtitle,
        style: theme.textTheme.bodySmall?.copyWith(color: cs.onSurfaceVariant),
      ),
      trailing: Icon(
        Icons.play_circle_fill_rounded,
        color: cs.primary,
        size: 36,
      ),
      enabled: enabled,
      onTap: onTap,
    );
  }
}

/// Thẻ một bài mẫu.
class _SampleCard extends StatelessWidget {
  const _SampleCard({
    required this.title,
    required this.subtitle,
    required this.tint,
    required this.busy,
    required this.enabled,
    required this.onTap,
  });

  final String title;
  final String subtitle;
  final int tint;
  final bool busy;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    final (bg, fg) = _tintColors(cs, tint);
    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: enabled ? onTap : null,
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              busy
                  ? const SizedBox(
                      width: 40,
                      height: 40,
                      child: Padding(
                        padding: EdgeInsets.all(8),
                        child: CircularProgressIndicator(strokeWidth: 2),
                      ),
                    )
                  : IconBadge(
                      Icons.school_rounded,
                      size: 40,
                      background: bg,
                      foreground: fg,
                    ),
              const SizedBox(height: 8),
              Text(title, style: theme.textTheme.titleMedium),
              const SizedBox(height: 4),
              Text(
                subtitle,
                style: theme.textTheme.bodySmall?.copyWith(
                  color: cs.onSurfaceVariant,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Bảng "Thêm bài hát". Trả true khi chọn từ Tệp.
class _AddSongSheet extends StatelessWidget {
  const _AddSongSheet();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final cs = theme.colorScheme;
    const steps = [
      'Mở tin nhắn có file nhạc',
      'Bấm giữ file → Mở bằng ứng dụng khác',
      'Chọn ChuẩnTone',
    ];
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('Thêm bài hát', style: theme.textTheme.titleLarge),
            const SizedBox(height: 12),
            Card(
              color: cs.surfaceContainer,
              clipBehavior: Clip.antiAlias,
              child: ListTile(
                contentPadding: const EdgeInsets.all(12),
                leading: const IconBadge(Icons.folder_rounded),
                title: Text('Chọn từ Tệp', style: theme.textTheme.titleMedium),
                subtitle: const Text(
                  'MP3, M4A, WAV — chọn kèm file lời .lrc nếu có',
                ),
                trailing: const Icon(Icons.chevron_right),
                onTap: () => Navigator.of(context).pop(true),
              ),
            ),
            const SizedBox(height: 12),
            Card(
              color: cs.surfaceContainer,
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        IconBadge(
                          Icons.chat_rounded,
                          background: cs.secondaryContainer,
                          foreground: cs.onSecondaryContainer,
                        ),
                        const SizedBox(width: 16),
                        Text('Gửi từ Zalo', style: theme.textTheme.titleMedium),
                      ],
                    ),
                    const SizedBox(height: 8),
                    for (final (i, step) in steps.indexed)
                      Padding(
                        padding: const EdgeInsets.only(top: 8),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            CircleAvatar(
                              radius: 12,
                              backgroundColor: cs.primaryContainer,
                              foregroundColor: cs.onPrimaryContainer,
                              child: Text(
                                '${i + 1}',
                                style: theme.textTheme.labelSmall?.copyWith(
                                  color: cs.onPrimaryContainer,
                                ),
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(child: Text(step)),
                          ],
                        ),
                      ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 12),
            Text(
              'ChuẩnTone không tải nhạc từ mạng — bạn dùng bài mình có.',
              textAlign: TextAlign.center,
              style: theme.textTheme.bodySmall?.copyWith(
                color: cs.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
