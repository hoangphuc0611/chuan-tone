import 'dart:async';

import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';

import 'data/sample_songs.dart';
import 'data/song_import.dart';
import 'data/song_library.dart';
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
      false => HomePage(
        engineInfo: widget.engineInfo,
        library: widget.library,
        initialAction: _action,
      ),
    };
  }
}

/// Trang chủ (M8 B3, bố cục ở docs/m8-plan.md mục 1): nút "Thêm bài hát" to nhất, "Hát tiếp" (bài
/// mới dùng — bấm là vào Màn hát với tone / thể loại / lời như lần trước), "Tất cả bài hát", bài
/// mẫu, bản thu, Cài đặt. Engine lỗi thì báo ngay đầu trang.
class HomePage extends StatefulWidget {
  const HomePage({
    super.key,
    required this.engineInfo,
    required this.library,
    this.takes,
    this.onSing,
    this.initialAction,
  });

  final EngineInfo engineInfo;
  final SongLibrary library;

  /// Việc chọn ở bước cuối của hướng dẫn lần đầu (thêm bài / hát bài mẫu).
  final OnboardingExit? initialAction;

  /// Kho bản thu (null = cùng thư mục dữ liệu với [library]).
  final TakeStore? takes;

  /// Mở Màn hát cho bài (mặc định [SingPage]); test thay bằng hàm giả.
  final Future<void> Function(BuildContext context, LibrarySong song)? onSing;

  /// Số bài ở "Hát tiếp".
  static const recentCount = 3;

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with RouteAware {
  List<LibrarySong>? _songs;
  int _takeCount = 0;
  String? _installing; // id bài mẫu đang chép vào thư viện

  late final TakeStore _takes =
      widget.takes ?? TakeStore(baseDir: widget.library.baseDir);

  bool get _ok => widget.engineInfo.ok;

  @override
  void initState() {
    super.initState();
    unawaited(_reload());
    final action = widget.initialAction;
    if (action != null && action != OnboardingExit.home) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted) return;
        if (action == OnboardingExit.addSong) {
          unawaited(_addSong());
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
  void didPopNext() => unawaited(_reload());

  Future<void> _reload() async {
    try {
      final songs = await widget.library.songs(
        engineVersion: _ok ? widget.engineInfo.version : null,
      );
      final takes = await _takes.list();
      if (!mounted) return;
      setState(() {
        _songs = songs;
        _takeCount = takes.length;
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

  Future<void> _sing(LibrarySong song) async {
    if (song.missing) {
      _snack(
        'File nhạc của bài này không còn trên máy. Hãy xoá rồi thêm lại bài.',
      );
      return;
    }
    final sing = widget.onSing;
    if (sing != null) {
      await sing(context, song);
    } else {
      await _push(SingPage(song: song.ref));
    }
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
    if (song != null) await _sing(song);
    unawaited(_reload());
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final songs = _songs;
    final recent = songs == null
        ? const <LibrarySong>[]
        : songs.take(HomePage.recentCount).toList();
    return Scaffold(
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 16, 16, 24),
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'ChuẩnTone',
                        style: theme.textTheme.headlineLarge?.copyWith(
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      Text(
                        'Bấm bài là hát',
                        style: theme.textTheme.titleMedium,
                      ),
                    ],
                  ),
                ),
                IconButton(
                  tooltip: 'Cài đặt',
                  iconSize: 28,
                  icon: const Icon(Icons.settings_outlined),
                  onPressed: () => _push(
                    SettingsPage(
                      engineInfo: widget.engineInfo,
                      library: widget.library,
                    ),
                  ),
                ),
              ],
            ),
            if (!_ok) ...[
              const SizedBox(height: 12),
              EngineStatusCard(info: widget.engineInfo),
            ],
            const SizedBox(height: 16),
            _BigButton(
              icon: Icons.library_add,
              label: 'Thêm bài hát',
              subtitle: 'Từ Tệp, Zalo, Safari…',
              enabled: _ok,
              onTap: _addSong,
            ),
            if (recent.isNotEmpty) ...[
              const _SectionTitle('Hát tiếp'),
              for (final s in recent)
                _SongRow(
                  title: s.title,
                  subtitle: _recentSubtitle(s),
                  missing: s.missing,
                  enabled: _ok,
                  onTap: () => _sing(s),
                ),
              ListTile(
                contentPadding: const EdgeInsets.symmetric(horizontal: 8),
                title: Text('Tất cả bài hát (${songs!.length})'),
                trailing: const Icon(Icons.chevron_right),
                enabled: _ok,
                onTap: () => _push(SongsPage(library: widget.library)),
              ),
            ],
            const _SectionTitle('Bài mẫu'),
            for (final s in sampleSongs)
              _SongRow(
                title: s.title,
                subtitle: s.subtitle,
                icon: Icons.school_outlined,
                busy: _installing == s.id,
                enabled: _ok,
                onTap: () => _openSample(s),
              ),
            const SizedBox(height: 16),
            FilledButton.tonalIcon(
              onPressed: _ok ? () => _push(TakesPage(store: _takes)) : null,
              icon: const Icon(Icons.graphic_eq),
              label: Text(
                _takeCount == 0
                    ? 'Bản thu của tôi'
                    : 'Bản thu của tôi ($_takeCount)',
              ),
              style: FilledButton.styleFrom(
                minimumSize: const Size.fromHeight(56),
                textStyle: theme.textTheme.titleMedium,
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

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 24, 8, 4),
      child: Text(
        text,
        style: theme.textTheme.titleMedium?.copyWith(
          color: theme.colorScheme.primary,
          fontWeight: FontWeight.w700,
        ),
      ),
    );
  }
}

/// Một bài ở Trang chủ: bấm cả dòng là vào hát.
class _SongRow extends StatelessWidget {
  const _SongRow({
    required this.title,
    required this.subtitle,
    required this.enabled,
    required this.onTap,
    this.icon = Icons.music_note,
    this.missing = false,
    this.busy = false,
  });

  final String title;
  final String subtitle;
  final bool enabled;
  final VoidCallback onTap;
  final IconData icon;
  final bool missing;
  final bool busy;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ListTile(
      contentPadding: const EdgeInsets.symmetric(horizontal: 8),
      minVerticalPadding: 10,
      leading: Icon(
        missing ? Icons.music_off : icon,
        color: missing ? theme.colorScheme.error : theme.colorScheme.primary,
      ),
      title: Text(
        title,
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
        style: theme.textTheme.titleMedium,
      ),
      subtitle: Text(subtitle),
      trailing: busy
          ? const SizedBox(
              width: 24,
              height: 24,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : Icon(Icons.play_circle_fill, color: theme.colorScheme.primary),
      enabled: enabled,
      onTap: onTap,
    );
  }
}

class _BigButton extends StatelessWidget {
  const _BigButton({
    required this.icon,
    required this.label,
    required this.subtitle,
    required this.enabled,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final String subtitle;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return FilledButton(
      onPressed: enabled ? onTap : null,
      style: FilledButton.styleFrom(
        minimumSize: const Size.fromHeight(84),
        alignment: Alignment.centerLeft,
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
      ),
      child: Row(
        children: [
          Icon(icon, size: 36),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                // Cỡ chữ của theme nhưng không lấy màu: chữ theo màu của nút (sáng trên nền đỏ).
                Text(
                  label,
                  style: TextStyle(
                    fontSize: theme.textTheme.titleLarge?.fontSize,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                Text(
                  subtitle,
                  style: TextStyle(
                    fontSize: theme.textTheme.bodyMedium?.fontSize,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
