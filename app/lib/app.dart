import 'package:chuantone_media/chuantone_media.dart';
import 'package:flutter/material.dart';

import 'data/song_import.dart';
import 'data/song_library.dart';
import 'engine_info.dart';
import 'pages/analysis_page.dart';
import 'pages/latency_page.dart';
import 'pages/songs_page.dart';
import 'pages/takes_page.dart';
import 'pages/test_room_page.dart';

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

  /// "Mở bằng ChuẩnTone": thêm các file vào thư viện rồi mở màn Phân tích bài của file cuối.
  Future<void> _onIncoming(List<IncomingFile> files) async {
    LibrarySong? last;
    var failed = 0;
    for (final f in files) {
      try {
        last = await _library.add(f.path, name: f.name, move: true);
      } catch (_) {
        failed++;
      }
    }
    if (failed > 0) {
      _messenger.currentState?.showSnackBar(
        SnackBar(
          content: Text(
            failed == 1
                ? 'Không thêm được file nhạc vừa mở.'
                : 'Không thêm được $failed file nhạc vừa mở.',
          ),
        ),
      );
    }
    final song = last;
    if (song == null || !widget.engineInfo.ok) return;
    await _navigator.currentState?.push(
      MaterialPageRoute<void>(builder: (_) => AnalysisPage(song: song.ref)),
    );
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ChuẩnTone',
      debugShowCheckedModeBanner: false,
      navigatorKey: _navigator,
      scaffoldMessengerKey: _messenger,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFFE5484D)),
        useMaterial3: true,
      ),
      home: HomePage(engineInfo: widget.engineInfo, library: _library),
    );
  }
}

class HomePage extends StatelessWidget {
  const HomePage({super.key, required this.engineInfo, required this.library});

  final EngineInfo engineInfo;
  final SongLibrary library;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      body: SafeArea(
        // Cuộn được khi màn hình thấp; đủ cao thì nút vẫn dồn xuống dưới (Spacer).
        child: LayoutBuilder(
          builder: (context, box) => SingleChildScrollView(
            padding: const EdgeInsets.all(24),
            child: ConstrainedBox(
              constraints: BoxConstraints(
                minHeight: (box.maxHeight - 48).clamp(0, double.infinity),
              ),
              child: IntrinsicHeight(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    const SizedBox(height: 32),
                    Text(
                      'ChuẩnTone',
                      style: theme.textTheme.displaySmall?.copyWith(
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Text('Bấm bài là hát', style: theme.textTheme.titleLarge),
                    const Spacer(),
                    _BigButton(
                      icon: Icons.library_add,
                      label: 'Thêm bài hát',
                      subtitle: 'Dò tone, nhịp — tách giọng ca sĩ nếu cần',
                      enabled: engineInfo.ok,
                      primary: true,
                      onTap: () => _addSong(context, library),
                    ),
                    const SizedBox(height: 12),
                    _BigButton(
                      icon: Icons.library_music,
                      label: 'Bài hát của tôi',
                      subtitle: 'Các bài đã thêm — tìm và hát lại',
                      enabled: engineInfo.ok,
                      onTap: () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => SongsPage(library: library),
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    _BigButton(
                      icon: Icons.mic_external_on,
                      label: 'Phòng thử',
                      subtitle: 'Phát nhạc, nghe lại giọng',
                      enabled: engineInfo.ok,
                      onTap: () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => const TestRoomPage(),
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    _BigButton(
                      icon: Icons.graphic_eq,
                      label: 'Bản thu của tôi',
                      subtitle: 'Nghe lại, chỉnh, xuất, chia sẻ',
                      enabled: engineInfo.ok,
                      onTap: () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => const TakesPage(),
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    _BigButton(
                      icon: Icons.speed,
                      label: 'Đo độ trễ',
                      subtitle: 'Kiểm tra máy có hát mượt không',
                      enabled: engineInfo.ok,
                      onTap: () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => const LatencyPage(),
                        ),
                      ),
                    ),
                    const SizedBox(height: 24),
                    _EngineStatusCard(info: engineInfo),
                    const SizedBox(height: 24),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Chọn file (app Tệp trên iOS, không cần quyền thư viện nhạc) → thư viện bài → màn Phân tích bài.
Future<void> _addSong(BuildContext context, SongLibrary library) async {
  final LibrarySong? song;
  try {
    song = await pickSongIntoLibrary(library);
  } catch (_) {
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text(
            'Không thêm được file này. Hãy thử file MP3, M4A hoặc WAV.',
          ),
        ),
      );
    }
    return;
  }
  if (song == null || !context.mounted) return;
  await Navigator.of(context).push(
    MaterialPageRoute<void>(builder: (_) => AnalysisPage(song: song!.ref)),
  );
}

class _BigButton extends StatelessWidget {
  const _BigButton({
    required this.icon,
    required this.label,
    required this.subtitle,
    required this.enabled,
    required this.onTap,
    this.primary = false,
  });

  final IconData icon;
  final String label;
  final String subtitle;
  final bool enabled;
  final VoidCallback onTap;
  final bool primary;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final style = FilledButton.styleFrom(
      minimumSize: const Size.fromHeight(80),
      alignment: Alignment.centerLeft,
      padding: const EdgeInsets.symmetric(horizontal: 20),
    );
    final child = Row(
      children: [
        Icon(icon, size: 36),
        const SizedBox(width: 16),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(label, style: theme.textTheme.titleLarge),
              Text(subtitle, style: theme.textTheme.bodyMedium),
            ],
          ),
        ),
      ],
    );
    final onPressed = enabled ? onTap : null;
    return primary
        ? FilledButton(onPressed: onPressed, style: style, child: child)
        : FilledButton.tonal(onPressed: onPressed, style: style, child: child);
  }
}

class _EngineStatusCard extends StatelessWidget {
  const _EngineStatusCard({required this.info});

  final EngineInfo info;

  @override
  Widget build(BuildContext context) {
    final ok = info.ok;
    final color = ok ? Colors.green : Theme.of(context).colorScheme.error;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            Icon(ok ? Icons.check_circle : Icons.error, color: color, size: 32),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    ok
                        ? 'ToneCore v${info.version} – engine OK'
                        : 'Không khởi động được engine',
                    key: const Key('engine-status'),
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  if (ok)
                    Text(
                      'API ${info.apiVersion} · đã xử lý thử ${info.framesProcessed} mẫu',
                    )
                  else if (info.error != null)
                    Text(info.error!),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
