import 'dart:async';

import 'package:flutter/material.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import '../data/app_settings.dart';
import '../data/song_library.dart';
import '../data/storage_usage.dart';
import '../engine/audio_service.dart';
import '../engine/labels.dart';
import '../engine/song_jobs.dart';
import '../engine_info.dart';
import '../widgets/separation_panel.dart';
import 'latency_page.dart';
import 'latency_wizard_page.dart';
import 'mic_page.dart';
import 'test_room_page.dart';

/// Phiên bản app (flutter tool truyền tên bản build lúc biên dịch).
const appVersion = String.fromEnvironment(
  'FLUTTER_BUILD_NAME',
  defaultValue: '1.0.0',
);

/// Cài đặt (M8 B4, docs/m8-plan.md mục 1): Âm thanh (đo độ trễ từng bước, micro, tự nghe lại
/// giọng), Xuất (chất lượng âm thanh, video 720p, cân bằng âm lượng), Bộ nhớ (dung lượng, xoá từng
/// nhóm), Trợ giúp (mẹo hát), Giới thiệu (phiên bản, giấy phép, quyền riêng tư), Nâng cao (Phòng thử,
/// đo độ trễ kỹ thuật, máy chủ tách giọng).
class SettingsPage extends StatefulWidget {
  const SettingsPage({
    super.key,
    required this.engineInfo,
    this.library,
    this.service,
  });

  final EngineInfo engineInfo;

  /// Thư viện bài (thư mục dữ liệu của bộ nhớ / cài đặt; null = của app).
  final SongLibrary? library;
  final AudioService? service;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  late final SongLibrary _library = widget.library ?? SongLibrary();
  late final AppSettings _settings = AppSettings(baseDir: _library.baseDir);
  late final AudioService _service = widget.service ?? AudioService.instance;
  ExportPrefs? _export;
  bool _autoMonitor = true;
  InputKind _input = InputKind.auto;
  String? _server;
  StorageUsage? _usage;

  bool get _ok => widget.engineInfo.ok;

  @override
  void initState() {
    super.initState();
    unawaited(_load());
  }

  Future<void> _load() async {
    final export = await _settings.exportPrefs();
    final auto = await _settings.autoMonitor();
    final input = await _settings.inputPreference();
    final server = await _settings.separationServer();
    if (!mounted) return;
    setState(() {
      _export = export;
      _autoMonitor = auto;
      _input = input;
      _server = server;
    });
    await _measure();
  }

  Future<void> _measure() async {
    final u = await measureStorage(baseDir: _library.baseDir);
    if (mounted) setState(() => _usage = u);
  }

  Future<void> _push(Widget page) async {
    await Navigator.of(context)
        .push(MaterialPageRoute<void>(builder: (_) => page));
    // Quay về: micro có thể đã đổi, bộ nhớ có thể đã khác.
    final input = await _settings.inputPreference();
    if (mounted) setState(() => _input = input);
    await _measure();
  }

  /// Đổi lựa chọn xuất từ giá trị đang có (bấm liên tiếp nhiều mục vẫn giữ đủ các thay đổi).
  Future<void> _updateExport(ExportPrefs Function(ExportPrefs p) change) async {
    final p = change(_export ?? const ExportPrefs());
    setState(() => _export = p);
    await _settings.setExportPrefs(p);
  }

  Future<void> _setAutoMonitor(bool on) async {
    setState(() => _autoMonitor = on);
    _service.updateAutoMonitor(on);
    await _settings.setAutoMonitor(on);
  }

  Future<void> _confirmDelete({
    required String title,
    required String body,
    required Future<void> Function() delete,
  }) async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title),
        content: Text(body),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Huỷ'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Xoá'),
          ),
        ],
      ),
    );
    if (yes != true) return;
    await delete();
    await _measure();
  }

  Future<String> _saveServer(String input) async {
    final r = await saveSeparationServer(_settings, input);
    if (r.changed && mounted) setState(() => _server = r.url);
    return r.message;
  }

  @override
  Widget build(BuildContext context) {
    final export = _export;
    final u = _usage;
    final base = _library.baseDir;
    return Scaffold(
      appBar: AppBar(title: const Text('Cài đặt')),
      body: ListView(
        padding: const EdgeInsets.only(bottom: 24),
        children: [
          const _Header('Âm thanh'),
          ListTile(
            leading: const Icon(Icons.speed),
            title: const Text('Đo độ trễ'),
            subtitle: const Text(
              'Biết máy hát có mượt không, căn giọng khi thu',
            ),
            enabled: _ok,
            onTap: () => _push(const LatencyWizardPage()),
          ),
          ListTile(
            leading: const Icon(Icons.mic_none),
            title: const Text('Micro'),
            subtitle: Text(inputKindLabel(_input)),
            enabled: _ok,
            onTap: () => _push(MicPage(service: widget.service)),
          ),
          SwitchListTile(
            secondary: const Icon(Icons.headphones),
            title: const Text('Tự bật nghe lại giọng khi cắm tai nghe'),
            subtitle: const Text('Tắt khi về loa ngoài để không bị hú'),
            value: _autoMonitor,
            onChanged: _setAutoMonitor,
          ),
          const _Header('Xuất'),
          if (export != null) ...[
            ListTile(
              leading: const Icon(Icons.high_quality_outlined),
              title: const Text('Chất lượng âm thanh'),
              subtitle: Text(
                '${export.aacBitrate ~/ 1000} kbit/s — cao hơn thì file nặng hơn',
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(72, 0, 16, 8),
              child: Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final (rate, label) in const [
                    (128000, 'Tiêu chuẩn'),
                    (192000, 'Cao'),
                    (256000, 'Rất cao'),
                  ])
                    ChoiceChip(
                      label: Text(label),
                      selected: export.aacBitrate == rate,
                      showCheckmark: false,
                      onSelected: (_) =>
                          _updateExport((p) => p.copyWith(aacBitrate: rate)),
                    ),
                ],
              ),
            ),
            SwitchListTile(
              secondary: const Icon(Icons.movie_outlined),
              title: const Text('Video 720p'),
              subtitle: const Text(
                'Nhẹ hơn khi gửi Zalo; tắt thì xuất 1080p nét hơn',
              ),
              value: export.hd720,
              onChanged: (v) => _updateExport((p) => p.copyWith(hd720: v)),
            ),
            SwitchListTile(
              secondary: const Icon(Icons.equalizer),
              title: const Text('Cân bằng âm lượng cho mạng xã hội'),
              subtitle: const Text(
                'Bài xuất to đều như nhạc trên TikTok, Facebook (−14 LUFS)',
              ),
              value: export.normalize,
              onChanged: (v) => _updateExport((p) => p.copyWith(normalize: v)),
            ),
          ],
          const _Header('Bộ nhớ'),
          _StorageRow(
            icon: Icons.library_music_outlined,
            label: 'Bài hát',
            bytes: u?.songs,
            onDelete: () => _confirmDelete(
              title: 'Xoá tất cả bài hát?',
              body:
                  'File nhạc, lời, giọng ca sĩ đã tách và kết quả dò tone của mọi bài sẽ bị xoá. '
                  'Bản thu vẫn được giữ.',
              delete: () => deleteAllSongs(_library),
            ),
          ),
          _StorageRow(
            icon: Icons.record_voice_over_outlined,
            label: 'Giọng ca sĩ đã tách',
            bytes: u?.stems,
            onDelete: () => _confirmDelete(
              title: 'Xoá giọng ca sĩ đã tách?',
              body: 'Bài vẫn hát được; muốn dùng "Giữ giọng ca sĩ gốc" thì phải tách giọng lại.',
              delete: () => deleteAllStems(baseDir: base),
            ),
          ),
          _StorageRow(
            icon: Icons.graphic_eq,
            label: 'Bản thu',
            bytes: u?.takes,
            onDelete: () => _confirmDelete(
              title: 'Xoá tất cả bản thu?',
              body:
                  'Mọi bản thu và video đã xuất sẽ bị xoá, không lấy lại được.',
              delete: () => deleteAllTakes(baseDir: base),
            ),
          ),
          _StorageRow(
            icon: Icons.movie_outlined,
            label: 'Video và âm thanh đã xuất',
            bytes: u?.exports,
            onDelete: () => _confirmDelete(
              title: 'Xoá file đã xuất?',
              body:
                  'Bản thu vẫn giữ; vào Bản thu của tôi để xuất lại khi cần. File đã lưu vào Ảnh '
                  'không bị xoá.',
              delete: () => deleteExports(baseDir: base),
            ),
          ),
          const _Header('Trợ giúp'),
          ListTile(
            leading: const Icon(Icons.tips_and_updates_outlined),
            title: const Text('Mẹo hát'),
            subtitle: const Text('Tai nghe, chỉnh tone, khi nào dùng Robot…'),
            onTap: () => _push(_TipsPage(settings: _settings)),
          ),
          const _Header('Giới thiệu'),
          ListTile(
            leading: const Icon(Icons.info_outline),
            title: const Text('ChuẩnTone'),
            subtitle: Text(
              'Phiên bản $appVersion'
              '${_ok ? ' · ToneCore ${widget.engineInfo.version}' : ''}',
            ),
          ),
          ListTile(
            leading: const Icon(Icons.privacy_tip_outlined),
            title: const Text('Quyền riêng tư'),
            subtitle: const Text('Khi nào âm thanh được gửi đi'),
            onTap: () => _push(const _PrivacyPage()),
          ),
          ListTile(
            leading: const Icon(Icons.description_outlined),
            title: const Text('Giấy phép thư viện'),
            onTap: () => showLicensePage(
              context: context,
              applicationName: 'ChuẩnTone',
              applicationVersion: appVersion,
              applicationLegalese:
                  'Engine âm thanh ToneCore do nhóm ChuẩnTone tự viết. Bài mẫu luyện giọng do '
                  'nhóm tự tạo.',
            ),
          ),
          if (!_ok) EngineStatusCard(info: widget.engineInfo),
          const _Header('Nâng cao'),
          ListTile(
            leading: const Icon(Icons.science_outlined),
            title: const Text('Phòng thử'),
            subtitle: const Text('Mọi nút và thông số kỹ thuật của engine'),
            enabled: _ok,
            onTap: () => _push(const TestRoomPage()),
          ),
          ListTile(
            leading: const Icon(Icons.timer_outlined),
            title: const Text('Đo độ trễ (kỹ thuật)'),
            subtitle: const Text(
              'Số đo chi tiết, lịch sử, sao chép gửi nhóm phát triển',
            ),
            enabled: _ok,
            onTap: () => _push(const LatencyPage()),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
            child: ServerSettingsCard(
              key: ValueKey(_server),
              initial: _server,
              onSave: _saveServer,
            ),
          ),
          if (_ok)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: EngineStatusCard(info: widget.engineInfo),
            ),
        ],
      ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 20, 16, 4),
      child: Text(
        text,
        style: theme.textTheme.titleSmall?.copyWith(
          color: theme.colorScheme.primary,
          fontWeight: FontWeight.w700,
        ),
      ),
    );
  }
}

/// Một nhóm bộ nhớ: dung lượng + nút xoá.
class _StorageRow extends StatelessWidget {
  const _StorageRow({
    required this.icon,
    required this.label,
    required this.bytes,
    required this.onDelete,
  });

  final IconData icon;
  final String label;
  final int? bytes;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    final b = bytes;
    return ListTile(
      leading: Icon(icon),
      title: Text(label),
      subtitle: Text(b == null ? 'Đang tính…' : formatBytes(b)),
      trailing: TextButton(
        onPressed: b == null || b == 0 ? null : onDelete,
        child: const Text('Xoá'),
      ),
    );
  }
}

/// Mẹo hát (Cài đặt → Trợ giúp).
class _TipsPage extends StatelessWidget {
  const _TipsPage({required this.settings});

  final AppSettings settings;

  static const _tips = <(IconData, String, String)>[
    (
      Icons.headphones,
      'Dùng tai nghe có dây',
      'Nghe lại giọng qua tai nghe có dây là rõ và ít trễ nhất. Loa ngoài dễ bị hú, tai nghe '
          'Bluetooth trễ nhiều nên app tắt nghe lại giọng.',
    ),
    (
      Icons.swap_vert,
      'Bài cao quá hay thấp quá',
      'Bấm Tone − / + ở Màn hát để hạ hoặc nâng tone nhạc (mỗi bậc nửa cung). App nhớ tone cho '
          'từng bài.',
    ),
    (
      Icons.auto_fix_high,
      'Chọn mức chỉnh giọng',
      '"Tự nhiên" nhẹ nhàng, hợp bolero, ballad. "Ca sĩ" kéo về nốt nhanh hơn, hợp nhạc trẻ. '
          '"Robot" là hiệu ứng giọng máy cho rap, remix — không dùng khi muốn giọng tự nhiên.',
    ),
    (
      Icons.music_note,
      'Tone dò sai?',
      'Bấm ⋮ → Sửa tone của bài ở Màn hát. Tone sai thì chỉnh giọng kéo về nốt lạ.',
    ),
    (
      Icons.record_voice_over,
      'Bài gốc còn giọng ca sĩ',
      'Bấm ⋮ → Tách giọng ca sĩ để hát karaoke trên bài gốc; thanh "Giữ giọng ca sĩ gốc" giúp hát '
          'theo khi chưa thuộc.',
    ),
    (
      Icons.mic,
      'Thu cho hay',
      'Để micro cách miệng một gang tay, chỗ yên tĩnh. Thu xong có thể đổi thể loại, chỉnh "Giọng '
          'sớm / muộn" ở màn Nghe lại.',
    ),
  ];

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(title: const Text('Mẹo hát')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          for (final (icon, title, body) in _tips)
            Padding(
              padding: const EdgeInsets.only(bottom: 16),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(icon, color: theme.colorScheme.primary, size: 28),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(title, style: theme.textTheme.titleMedium),
                        const SizedBox(height: 2),
                        Text(body, style: theme.textTheme.bodyMedium),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          OutlinedButton.icon(
            onPressed: () async {
              await settings.resetTip('sing');
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(
                    content: Text(
                      'Lần tới vào Màn hát sẽ hiện lại bong bóng mẹo.',
                    ),
                  ),
                );
              }
            },
            icon: const Icon(Icons.replay),
            label: const Text('Xem lại mẹo ở Màn hát'),
          ),
        ],
      ),
    );
  }
}

/// Quyền riêng tư: khi nào âm thanh rời khỏi máy (CLAUDE.md mục 7).
class _PrivacyPage extends StatelessWidget {
  const _PrivacyPage();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    Widget para(String title, String body) => Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: theme.textTheme.titleMedium),
          const SizedBox(height: 4),
          Text(body, style: theme.textTheme.bodyMedium),
        ],
      ),
    );
    return Scaffold(
      appBar: AppBar(title: const Text('Quyền riêng tư')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          para(
            'Trên máy bạn',
            'Hát, chỉnh giọng, dò tone, thu âm và xuất video đều chạy ngay trên máy, không cần '
                'mạng. Bài hát, lời và bản thu nằm trong vùng dữ liệu riêng của app.',
          ),
          para(
            'Khi nào âm thanh được gửi đi',
            'Chỉ khi bạn bấm "Tách giọng ca sĩ" và đồng ý: app gửi bài đó (nén FLAC) lên máy chủ '
                'tách giọng, nhận về phần giọng ca sĩ. Máy chủ tự xoá file sau 24 giờ.',
          ),
          para(
            'Mã máy',
            'App tạo một mã ngẫu nhiên (không gắn với tên, số điện thoại hay tài khoản) để máy chủ '
                'giới hạn số lần tách và giữ việc tách của máy bạn riêng.',
          ),
          para(
            'Chia sẻ',
            'Video / âm thanh chỉ rời app khi bạn bấm Chia sẻ hoặc Lưu vào Ảnh.',
          ),
        ],
      ),
    );
  }
}

/// Phiên bản engine / lỗi khởi động engine.
class EngineStatusCard extends StatelessWidget {
  const EngineStatusCard({super.key, required this.info});

  final EngineInfo info;

  @override
  Widget build(BuildContext context) {
    final ok = info.ok;
    final color = ok ? Colors.green : Theme.of(context).colorScheme.error;
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
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
