// Lượt chạy toàn app như người thử (M8 B7 — phần máy làm được của vòng thử nhỏ, docs/m8-usability.md):
// mở app lần đầu → hướng dẫn 6 bước → nhận bài "Mở bằng ChuẩnTone" (kèm lời gửi riêng) → Phân tích
// bài → Màn hát (mẹo lần đầu, chỉnh giọng, hạ tone, Bolero, Vang) → thu 30 giây → Nghe lại (nhạc nhỏ
// đi, xuất M4A thật, xuất video + lưu Ảnh qua kênh native giả) → Trang chủ → mở lại bài, bản thu.
// App thật (ChuanToneApp), engine thật backend NULL: test chạy đồng hồ âm thanh (processOffline),
// micro = giọng giả hát lệch nốt + tiếng loa lọt vào. Màn cỡ iPhone 13 (390×844), giao diện iOS.
//   TONECORE_LIB=../engine/build/host/libtonecore.dylib fvm flutter test test/walkthrough_test.dart
// Thêm (máy dev):
//   WALKTHROUGH_OUT=../testdata/walkthrough   chụp màn hình từng bước (font Roboto thay SF) + nhật ký
//   WALKTHROUGH_SONG=/duong/dan/bai.m4a        bài thật thay vòng hoà âm giả
//   CHUANTONE_SERVER=http://127.0.0.1:8000     tách giọng với server thật (Cài đặt → Nâng cao)

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:chuantone/app.dart';
import 'package:chuantone/data/app_settings.dart';
import 'package:chuantone/data/library_db.dart';
import 'package:chuantone/data/song_library.dart';
import 'package:chuantone/data/test_log.dart';
import 'package:chuantone/data/wav_io.dart';
import 'package:chuantone/engine/audio_service.dart';
import 'package:chuantone/engine_info.dart';
import 'package:chuantone/pages/analysis_page.dart';
import 'package:chuantone/widgets/analysis_widgets.dart';
import 'package:chuantone/widgets/error_text.dart';
import 'package:chuantone/widgets/gain_slider.dart';
import 'package:chuantone/widgets/separation_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:tonecore_flutter/tonecore_flutter.dart';

import 'helpers/engine_audio.dart';

final _env = Platform.environment;
final String? _out = _env['WALKTHROUGH_OUT'];
final String? _server = _env['CHUANTONE_SERVER'];

/// Bộ nén AAC của hệ điều hành (macOS); bản host trên Linux (CI) không có → xuất báo lỗi.
final bool _hasAac = Platform.isMacOS;

/// Thư mục tạm của Dart = thư mục dữ liệu của app trên máy tính ([appDataDir]): đổi sang thư mục
/// riêng của lượt chạy để app thật không đụng file của máy.
final class _AppDir extends IOOverrides {
  _AppDir(this.dir);
  final Directory dir;
  @override
  Directory getSystemTempDirectory() => dir;
}

/// "Điện thoại" của lượt chạy: luồng âm thanh (engine backend NULL chạy theo đồng hồ của test),
/// micro = giọng hát + tiếng loa lọt vào, các kênh native giả (quyền micro, file gửi vào, xuất
/// video, lưu Ảnh, chia sẻ).
class _Phone {
  _Phone(this.service);

  final AudioService service;
  static const rate = 48000;
  static const block = 256;

  /// Đang hát vào micro.
  bool singing = false;

  /// Tiếng loa lọt vào micro (loa ngoài); 0 khi đeo tai nghe.
  double leak = 0.25;
  static const leakDelay = 960; // 20 ms: loa → micro qua bộ đệm của máy

  int _t = 0;
  double _phase = 0;
  final _played = Float32List(8192);

  // Kênh native giả.
  var micStatus = 'undetermined';
  final incoming = <Map<String, String>>[];
  final calls = <String>[];
  final saved = <String>[];

  /// Giọng giả: đi lên xuống gam Rê trưởng, mỗi nốt 0,6 s, hát cao hơn nốt ~35 cent kèm rung giọng.
  double _voice(int t) {
    const scale = [62, 64, 66, 67, 69, 67, 66, 64];
    final note = scale[(t ~/ (rate * 0.6)) % scale.length];
    final cents = 35 + 20 * math.sin(2 * math.pi * 5.5 * t / rate);
    final hz = 440 * math.pow(2, (note - 69 + cents / 100) / 12);
    _phase += 2 * math.pi * hz / rate;
    return 0.2 * math.sin(_phase) + 0.06 * math.sin(2 * _phase);
  }

  /// Chạy [frames] mẫu âm thanh (khi engine đã mở).
  void run(int frames) {
    final e = service.engine;
    if (e == null) {
      _t += frames;
      return;
    }
    while (frames > 0) {
      final n = math.min(block, frames);
      final mic = Float32List(n);
      for (var i = 0; i < n; i++) {
        final t = _t + i;
        var s = singing ? _voice(t) : 0.0;
        s += leak * _played[(t - leakDelay) & (_played.length - 1)];
        mic[i] = s;
      }
      final out = e.processOffline(mic, n);
      for (var i = 0; i < n; i++) {
        _played[(_t + i) & (_played.length - 1)] =
            0.5 * (out[2 * i] + out[2 * i + 1]);
      }
      _t += n;
      frames -= n;
    }
  }

  void installChannels(WidgetTester tester) {
    final m = tester.binding.defaultBinaryMessenger;
    m.setMockMethodCallHandler(const MethodChannel('chuantone/system'), (
      call,
    ) async {
      calls.add('system.${call.method}');
      return switch (call.method) {
        'micPermission' => micStatus,
        'requestMic' => (micStatus = 'granted') == 'granted',
        _ => null,
      };
    });
    m.setMockMethodCallHandler(const MethodChannel('chuantone/incoming'), (
      call,
    ) async {
      if (call.method != 'take') return null;
      final files = List.of(incoming);
      incoming.clear();
      return files;
    });
    m.setMockMethodCallHandler(const MethodChannel('chuantone/media'), (
      call,
    ) async {
      calls.add('media.${call.method}');
      final args = (call.arguments as Map?) ?? const {};
      switch (call.method) {
        case 'renderWaveformVideo':
          // Video thật do AVFoundation dựng trên iPhone; ở đây chỉ cần file kết quả.
          File(args['output'] as String).writeAsBytesSync(Uint8List(1024));
          return true;
        case 'progress':
          return 1.0;
        case 'saveVideoToPhotos':
          saved.add(args['path'] as String);
          return true;
        case 'share':
          return true;
        case 'beginBackground':
          return 1;
        case 'cameraStart':
          return false; // máy tính: không có camera
        default:
          return null;
      }
    });
  }

  /// Gửi file vào app như "Mở bằng ChuẩnTone" từ Zalo.
  Future<void> receive(WidgetTester tester, String path) async {
    incoming.add({'path': path, 'name': path.split('/').last});
    await tester.binding.defaultBinaryMessenger.handlePlatformMessage(
      'chuantone/incoming',
      const StandardMethodCodec().encodeMethodCall(
        const MethodCall('available'),
      ),
      (_) {},
    );
  }
}

/// Chụp màn hình từng bước vào [out] (số thứ tự + tên bước).
class _Camera {
  _Camera(this.key, this.out);
  final GlobalKey key;
  final String? out;
  int _n = 0;
  final names = <String>[];

  Future<void> shot(WidgetTester tester, String name) async {
    final out = this.out;
    if (out == null) return;
    await tester.pump();
    final boundary =
        key.currentContext!.findRenderObject()! as RenderRepaintBoundary;
    final file = '$out/${(++_n).toString().padLeft(2, '0')}-$name.png';
    names.add(file.split('/').last);
    await tester.runAsync(() async {
      final image = await boundary.toImage(pixelRatio: 2);
      final png = await image.toByteData(format: ui.ImageByteFormat.png);
      image.dispose();
      File(file).writeAsBytesSync(png!.buffer.asUint8List());
    });
  }
}

/// Font thật (test mặc định vẽ chữ thành ô vuông rộng 1 em): Be Vietnam Pro của app + Material
/// Icons kèm Flutter SDK (cache `material_fonts`, có cả trên CI).
Future<void> _loadFonts() async {
  Future<ByteData> font(String path) async =>
      ByteData.sublistView(File(path).readAsBytesSync());
  final app = FontLoader('BeVietnamPro');
  for (final f in const ['Regular', 'SemiBold', 'Bold']) {
    app.addFont(font('assets/fonts/BeVietnamPro-$f.ttf'));
  }
  await app.load();
  var root = _env['FLUTTER_ROOT'];
  if (root == null) {
    // flutter_tester nằm ở <flutter>/bin/cache/artifacts/engine/<máy>/flutter_tester
    var d = File(Platform.resolvedExecutable).parent;
    for (var i = 0; i < 5; i++) {
      d = d.parent;
    }
    root = d.path;
  }
  final icons =
      '$root/bin/cache/artifacts/material_fonts/MaterialIcons-Regular.otf';
  if (File(icons).existsSync()) {
    await (FontLoader('MaterialIcons')..addFont(font(icons))).load();
  }
}

/// Bài giả 64 giây (vòng hoà âm 16 giây lặp 4 lần): đủ dài để thu 30 giây như nhiệm vụ 5 (hết
/// bài khi đang thu thì app tự dừng thu).
void _writeSong(String path) {
  writeCadenceWav(path);
  final w = readWavFile(path);
  final long = Float32List(w.samples.length * 4);
  for (var k = 0; k < 4; k++) {
    long.setAll(k * w.samples.length, w.samples);
  }
  File(path).writeAsBytesSync(
    encodeWav16(long, sampleRate: w.sampleRate, channels: w.channels),
  );
}

/// Lời thử (tự viết), mỗi câu 4 giây.
const _lyrics = '''
[00:00.50]Câu hát thử thứ nhất
[00:04.50]Câu thứ hai ngân dài hơn
[00:08.50]Câu thứ ba lên cao
[00:12.50]Câu cuối về lại nốt Rê
[00:16.50]Lặp lại câu thứ nhất
[00:20.50]Lặp lại câu thứ hai
[00:24.50]Lặp lại câu thứ ba
[00:28.50]Hết bài
''';

/// Máy thử: iPhone 13 giao diện sáng; iPhone SE (màn nhỏ nhất còn bán) giao diện tối + chữ to
/// 130% (người lớn tuổi hay bật) — tràn chữ / tràn bố cục làm test hỏng.
const _devices = [
  (
    name: 'iphone13',
    size: Size(1170, 2532),
    ratio: 3.0,
    top: 47.0, // tai thỏ (điểm ảnh logic)
    bottom: 34.0, // thanh vuốt về màn chính
    dark: false,
    text: 1.0,
  ),
  (
    name: 'iphone-se-toi-chu-to',
    size: Size(750, 1334),
    ratio: 2.0,
    top: 20.0,
    bottom: 0.0,
    dark: true,
    text: 1.3,
  ),
];

void main() {
  final skip = engineSkipReason();
  late Directory dir;
  final appKey = GlobalKey();

  setUpAll(() async {
    if (_out != null) {
      for (final d in _devices) {
        final dir = Directory('$_out/${d.name}');
        if (dir.existsSync()) dir.deleteSync(recursive: true);
        dir.createSync(recursive: true);
      }
    }
    // Font thật (cả khi không chụp): bố cục, xuống dòng như trên máy — font mặc định của test
    // rộng gấp đôi chữ thật, làm tràn giả trên màn nhỏ chữ to.
    await _loadFonts();
    // testWidgets thay HttpClient bằng bản trả 400; tách giọng với server thật cần mạng thật.
    if (_server != null) HttpOverrides.global = null;
  });

  setUp(() {
    dir = Directory.systemTemp.createTempSync('walkthrough');
    IOOverrides.global = _AppDir(dir);
  });

  tearDown(() {
    IOOverrides.global = null;
    TestLog.reset();
    dir.deleteSync(recursive: true);
  });

  for (final device in _devices) {
    testWidgets(
      'người thử mới (${device.name}): hướng dẫn → thêm bài → hát có chỉnh giọng → thu → xuất → mở lại',
      (tester) async {
        tester.view.physicalSize = device.size;
        tester.view.devicePixelRatio = device.ratio;
        final pad = FakeViewPadding(
          top: device.top * device.ratio,
          bottom: device.bottom * device.ratio,
        );
        tester.view.padding = pad;
        tester.view.viewPadding = pad;
        tester.platformDispatcher.textScaleFactorTestValue = device.text;
        tester.platformDispatcher.platformBrightnessTestValue = device.dark
            ? Brightness.dark
            : Brightness.light;
        addTearDown(tester.view.reset);
        addTearDown(tester.platformDispatcher.clearAllTestValues);
        final out = _out == null ? null : '$_out/${device.name}';
        final withServer = _server != null && device == _devices.first;

        final service = AudioService.test(
          () => ToneCoreEngine.create(maxBlockFrames: _Phone.block),
          settings: AppSettings(),
        );
        AudioService.instanceForTesting = service;
        final phone = _Phone(service)..installChannels(tester);
        final camera = _Camera(appKey, out);
        final notes = <String>[]; // số đo / điều thấy được, ghi ra ghi-chu.txt

        /// Chạy app + âm thanh tới khi [done] (tối đa [max]); mỗi nhịp 20 ms.
        Future<void> wait(
          bool Function() done, {
          Duration max = const Duration(seconds: 20),
          String? reason,
          int realMs = 2,
        }) async {
          const step = Duration(milliseconds: 20);
          for (var t = Duration.zero; t < max && !done(); t += step) {
            await tester.runAsync(
              () => Future<void>.delayed(Duration(milliseconds: realMs)),
            );
            // Luồng âm thanh chạy trước khi app xử lý sự kiện (máy thật: callback ~5 ms, app
            // hỏi sự kiện ~16 ms một lần).
            phone.run(_Phone.rate * step.inMilliseconds ~/ 1000);
            await tester.pump(step);
          }
          expect(done(), isTrue, reason: reason ?? 'hết giờ chờ');
        }

        Future<void> idle(Duration d) async {
          var left = d;
          await wait(() {
            left -= const Duration(milliseconds: 20);
            return left <= Duration.zero;
          }, max: d + const Duration(seconds: 1));
        }

        /// Màn hát nạp bài xong (hết thanh chạy ở đầu màn).
        Future<void> songLoaded() => wait(
          () => find
              .byWidgetPredicate(
                (w) => w is LinearProgressIndicator && w.value == null,
              )
              .evaluate()
              .isEmpty,
          max: const Duration(seconds: 90),
          realMs: 10,
          reason: 'nạp bài chưa xong',
        );

        Future<void> shot(String name) async {
          await idle(const Duration(milliseconds: 800));
          await camera.shot(tester, name);
        }

        bool shows(String text) => find.text(text).evaluate().isNotEmpty;
        bool showsPart(String text) =>
            find.textContaining(text).evaluate().isNotEmpty;

        /// Như người dùng: chưa thấy thì cuộn màn xuống tìm.
        Future<void> scrollTo(Finder f) async {
          // Cuộn xuống tìm; không thấy thì cuộn lên.
          for (final delta in const [300.0, -300.0]) {
            if (f.evaluate().isNotEmpty) break;
            try {
              await tester.scrollUntilVisible(
                f,
                delta,
                maxScrolls: 30,
                scrollable: find
                    .byWidgetPredicate(
                      (w) =>
                          w is Scrollable &&
                          w.axisDirection == AxisDirection.down,
                    )
                    .hitTestable()
                    .first,
              );
            } on StateError {
              // hết chỗ cuộn theo chiều này
            }
          }
          await tester.ensureVisible(f.first);
        }

        Future<void> tapOn(Finder f) async {
          await scrollTo(f);
          await tester.pump(const Duration(milliseconds: 300));
          // Bị che (vd thông báo dưới đáy màn che nút trên máy nhỏ, chữ to): chờ như người dùng.
          await wait(
            () => f.hitTestable().evaluate().isNotEmpty,
            max: const Duration(seconds: 6),
            reason: 'nút bị che: $f',
          );
          await tester.tap(f.hitTestable().first);
          await tester.pump();
        }

        Future<void> tapText(String text) => tapOn(find.text(text));
        Future<void> tapTip(String tooltip) => tapOn(find.byTooltip(tooltip));
        Future<void> tapChip(String label) =>
            tapOn(find.widgetWithText(ChoiceChip, label));

        /// Kéo thanh trượt [label] tới [fraction] (0…1) chiều dài.
        Future<void> slide(String label, double fraction) async {
          // Thanh trên cùng (bảng dưới che màn sau có thể có thanh cùng tên).
          final slider = find
              .descendant(
                of: find.widgetWithText(GainSlider, label),
                matching: find.byType(Slider),
              )
              .last;
          await scrollTo(slider);
          await tester.pump(const Duration(milliseconds: 300));
          final box = tester.getRect(slider);
          // Slider Material chừa 24 px mỗi bên.
          final x = box.left + 24 + (box.width - 48) * fraction;
          await tester.tapAt(Offset(x, box.center.dy));
          await tester.pump();
        }

        Future<void> back() async {
          await tester.tap(find.byType(BackButton).hitTestable().first);
          await tester.pump();
        }

        /// Đóng bảng dưới (bấm ra ngoài).
        Future<void> closeSheet() async {
          await tester.tapAt(const Offset(20, 60));
          await idle(const Duration(milliseconds: 600));
        }

        /// Về tab Trang chủ: đóng các màn đè lên khung tab (bảng dưới thì bấm ra ngoài).
        Future<void> toHome() async {
          while (find.byType(NavigationBar).hitTestable().evaluate().isEmpty) {
            if (find.byType(BackButton).hitTestable().evaluate().isEmpty) {
              await tester.tapAt(const Offset(20, 60));
            } else {
              await back();
            }
            await idle(const Duration(milliseconds: 800));
          }
          await tapText('Trang chủ');
          await wait(() => shows('Thêm bài hát'));
        }

        // Thời gian thật trên máy chạy test (Mac), không phải iPhone.
        Duration elapsed(DateTime since) => DateTime.now().difference(since);
        String secs(Duration d) =>
            '${(d.inMilliseconds / 1000).toStringAsFixed(1)} s';

        // Nhật ký thử nghiệm bật sẵn như buổi thử (đồng hồ của nhật ký = đồng hồ của test).
        await tester.runAsync(() async {
          await AppSettings().setTestLog(true);
          TestLog.now = () => tester.binding.clock.now();
          await TestLog.init(
            session: {'version': 'walkthrough', 'model': 'iPhone 13 (giả)'},
          );
        });

        // ---- Nhiệm vụ 1: mở app lần đầu, làm theo hướng dẫn ----
        await tester.pumpWidget(
          RepaintBoundary(
            key: appKey,
            child: ChuanToneApp(
              engineInfo: EngineInfo.probe(),
              library: SongLibrary(),
            ),
          ),
        );
        await wait(() => shows('Bắt đầu'));
        await shot('huong-dan-1-chao');
        await tapText('Bắt đầu');
        await wait(() => shows('Cho phép dùng micro'));
        await shot('huong-dan-2-micro');
        await tapText('Cho phép dùng micro');
        await wait(() => showsPart('Đo nhanh'));
        await shot('huong-dan-3-do');
        // Đặt máy trên bàn, loa ngoài: micro nghe lại tiếng xung đo.
        await wait(() => service.engine != null, reason: 'engine chưa mở');
        await tester.tap(find.byKey(const ValueKey('onboarding-measure')));
        await idle(const Duration(milliseconds: 200));
        final measureStart = DateTime.now();
        await wait(
          () => !showsPart('Đang đo'),
          max: const Duration(seconds: 30),
        );
        notes.add('Đo nhanh: ${secs(elapsed(measureStart))}');
        await shot('huong-dan-3-do-xong');
        await tapText('Tiếp');
        await wait(() => shows('Tai nghe'));
        await shot('huong-dan-4-tai-nghe-chua-cam');
        // Cắm tai nghe có dây: hết tiếng loa lọt vào micro.
        phone.leak = 0;
        service.engine!.setRouteHint(AudioRoute.wired);
        await idle(const Duration(milliseconds: 600));
        await shot('huong-dan-4-tai-nghe-da-cam');
        await tapText('Tiếp');
        await wait(() => shows('Thử giọng'));
        phone.singing = true;
        await idle(const Duration(seconds: 1));
        await shot('huong-dan-5-thu-giong');
        await tapText('Rõ rồi');
        phone.singing = false;
        await wait(() => shows('Xong! Hát thôi'));
        await shot('huong-dan-6-xong');

        await tapText('Về Trang chủ');
        await wait(() => shows('Thêm bài hát'));
        await shot('trang-chu-trong');
        await tapText('Thêm bài hát');
        await wait(() => shows('Chọn từ Tệp'));
        await shot('bang-them-bai');
        await closeSheet();

        // ---- Nhiệm vụ 2: bài gửi qua Zalo → "Mở bằng ChuẩnTone" → hát có chỉnh giọng ----
        final inbox = Directory('${dir.path}/zalo')..createSync();
        final songSrc = _env['WALKTHROUGH_SONG'];
        final ext = songSrc == null ? 'wav' : songSrc.split('.').last;
        final song = '${inbox.path}/Bai hat thu.$ext';
        if (songSrc != null) {
          File(songSrc).copySync(song);
        } else {
          _writeSong(song);
        }
        final added = DateTime.now();
        await phone.receive(tester, song);
        await wait(
          () => shows('Phân tích bài'),
          reason: 'không mở màn Phân tích',
        );
        final singButton = find.descendant(
          of: find.byType(AnalysisPage),
          matching: find.widgetWithText(FilledButton, 'Hát'),
        );
        bool singReady() {
          final b = singButton;
          return find.byType(KeyResultCard).evaluate().isNotEmpty &&
              tester.widget<ButtonStyleButton>(b.first).enabled;
        }

        await wait(
          singReady,
          max: const Duration(seconds: 60),
          reason: 'phân tích chưa xong',
        );
        notes.add('Phân tích bài: ${secs(elapsed(added))}');
        await shot('phan-tich-xong');
        // Lời gửi riêng sau (cùng tên bài).
        File('${inbox.path}/Bai hat thu.lrc').writeAsStringSync(_lyrics);
        await phone.receive(tester, '${inbox.path}/Bai hat thu.lrc');
        await wait(() => showsPart('Đã gắn lời'), reason: 'không gắn được lời');
        await shot('nhan-loi');
        await tapOn(singButton);
        final openStart = DateTime.now();
        await shot('man-hat-dang-mo');
        await wait(
          () => showsPart('Tiếp (1/'),
          max: const Duration(seconds: 90),
          realMs: 10,
          reason: 'không có mẹo lần đầu',
        );
        notes.add('Mở Màn hát (nạp bài): ${secs(elapsed(openStart))}');
        await shot('man-hat-meo-1');
        await tapOn(find.textContaining('Tiếp (1/'));
        await shot('man-hat-meo-2');
        await tapOn(find.textContaining('Tiếp (2/'));
        await shot('man-hat-meo-3');
        await tapText('Đã hiểu');
        await shot('man-hat');
        await tapTip('Phát');
        phone.singing = true;
        await idle(const Duration(seconds: 2));
        await tapChip('Tự nhiên');
        await idle(const Duration(seconds: 3));
        await shot('man-hat-dang-hat-tu-nhien');

        // ---- Nhiệm vụ 3: hạ tone ----
        await tapTip('Hạ tone');
        await idle(const Duration(seconds: 2));
        await shot('man-hat-ha-tone');

        // ---- Nhiệm vụ 4: Bolero, Vang to hơn ----
        await tapChip('Bolero');
        await idle(const Duration(seconds: 1));
        await slide('Vang', 0.7);
        await shot('man-hat-bolero-vang');

        // ---- Nhiệm vụ 5: thu 30 giây → nghe lại → nhạc nhỏ đi → xuất video → lưu Ảnh ----
        await tapText('Thu');
        final recStart = DateTime.now();
        await wait(() => showsPart('Dừng'), reason: 'không bắt đầu thu');
        notes.add('Bấm Thu → bắt đầu thu: ${secs(elapsed(recStart))}');
        await idle(const Duration(seconds: 5));
        await shot('dang-thu');
        await idle(const Duration(seconds: 25));
        await tapOn(find.textContaining('Dừng'));
        phone.singing = false;
        await wait(
          () => shows('Nghe lại & chỉnh'),
          max: const Duration(seconds: 30),
          reason: 'không vào màn Nghe lại',
        );
        await wait(
          () =>
              find.byTooltip('Phát').evaluate().isNotEmpty ||
              find.byIcon(Icons.play_arrow).evaluate().isNotEmpty,
        );
        await shot('nghe-lai');
        await tapOn(find.byIcon(Icons.play_arrow));
        await idle(const Duration(seconds: 3));
        await slide('Âm lượng nhạc', 0.25);
        await shot('nghe-lai-nhac-nho');
        final exportStart = DateTime.now();
        await tapText('Xuất video sóng nhạc (9:16)');
        if (!_hasAac) {
          // Linux (CI) không có bộ nén AAC: báo lỗi xuất bằng câu thường, không treo.
          await wait(
            () => showsPart('Xuất chưa được'),
            max: const Duration(seconds: 60),
            reason: 'không báo lỗi xuất',
          );
          await shot('xuat-loi-khong-co-aac');
        } else {
          await wait(
            () => shows('Lưu vào Ảnh'),
            max: const Duration(seconds: 60),
            reason: 'xuất video chưa xong',
          );
          notes.add(
            'Xuất video (trộn M4A thật, dựng video giả): '
            '${secs(elapsed(exportStart))}',
          );
          await shot('xuat-video-xong');
          // Bảng "Video đã sẵn sàng" (kèm thẻ kết quả ở cuối trang phía sau).
          expect(shows('Video đã sẵn sàng'), isTrue);
          await tapOn(find.text('Lưu vào Ảnh').hitTestable());
          await wait(() => phone.saved.isNotEmpty);
          await shot('luu-anh');
          final m4aStart = DateTime.now();
          await tapText('Xuất âm thanh (M4A)');
          await idle(const Duration(milliseconds: 300));
          await wait(
            () => find
                .byKey(const ValueKey('review-export-main'))
                .evaluate()
                .isNotEmpty,
            max: const Duration(seconds: 60),
            reason: 'xuất M4A chưa xong',
          );
          notes.add('Xuất M4A (đã trộn sẵn): ${secs(elapsed(m4aStart))}');
          await shot('xuat-m4a-xong');
        }

        // Nghe lại xong quay về Màn hát hát tiếp: bài phải còn nguyên (nạp lại, giữ tone đã hạ).
        await back();
        await wait(() => find.byTooltip('Hạ tone').evaluate().isNotEmpty);
        await idle(const Duration(seconds: 1));
        await songLoaded();
        await shot('man-hat-sau-nghe-lai');
        await tapTip('Âm lượng');
        await wait(() => shows('Nghe lại giọng'));
        await shot('bang-am-luong');
        await closeSheet();
        await tapTip('Thêm');
        await idle(const Duration(milliseconds: 500));
        await tapOn(find.widgetWithText(ListTile, 'Sửa tone của bài'));
        await wait(() => shows('Trưởng'));
        await shot('bang-sua-tone');
        await tapText('Huỷ');
        await idle(const Duration(milliseconds: 600));
        await tapTip('Thêm');
        await idle(const Duration(milliseconds: 500));
        await tapOn(find.widgetWithText(ListTile, 'Lời bài hát'));
        await wait(() => shows('Lời đúng như file'));
        await shot('bang-loi');
        await closeSheet();
        expect(
          service.engine!.playback.duration,
          greaterThan(Duration.zero),
          reason: 'Màn hát mất bài sau khi quay về từ Nghe lại',
        );
        expect(find.text('Tone \u22121'), findsOneWidget);

        // ---- Nhiệm vụ 6: về Trang chủ → mở lại bài → mở lại bản thu ----
        await toHome();
        await shot('trang-chu-co-bai');
        await tapOn(find.text('Bai hat thu').first);
        await wait(() => find.byTooltip('Hạ tone').evaluate().isNotEmpty);
        await idle(
          const Duration(milliseconds: 600),
        ); // hết hiệu ứng chuyển màn
        await camera.shot(tester, 'mo-lai-bai-dang-nap');
        await songLoaded();
        await shot('mo-lai-bai');
        // K4: mẹo lần đầu chỉ hiện một lần; bài nhớ tone đã hạ.
        expect(find.textContaining('Tiếp (1/'), findsNothing);
        expect(find.text('Tone \u22121'), findsOneWidget);
        await back();
        await idle(const Duration(milliseconds: 800));
        await tapOn(find.textContaining('Bản thu của tôi'));
        await idle(const Duration(seconds: 1));
        await shot('ban-thu-cua-toi');
        await tapOn(find.textContaining('Bai hat thu').first);
        await wait(() => shows('Nghe lại & chỉnh'));
        await shot('mo-lai-ban-thu');
        await toHome();

        // ---- Cài đặt ----
        await tapTip('Cài đặt');
        await idle(const Duration(seconds: 1));
        await shot('cai-dat');
        await tester.scrollUntilVisible(
          find.text('Ghi nhật ký thử nghiệm'),
          300,
        );
        await shot('cai-dat-nang-cao');
        await tapText('Micro');
        await idle(const Duration(seconds: 1));
        await shot('micro');
        await back();
        await idle(const Duration(milliseconds: 800));
        await tapText('Đo độ trễ');
        await idle(const Duration(seconds: 1));
        await shot('do-do-tre');
        await back();
        await idle(const Duration(milliseconds: 800));
        await toHome();

        // ---- Tách giọng với server thật (K12, chỉ khi có CHUANTONE_SERVER) ----
        if (withServer) {
          await tapTip('Cài đặt');
          await idle(const Duration(seconds: 1));
          final field = find.descendant(
            of: find.byType(ServerSettingsCard),
            matching: find.byType(TextField),
          );
          await scrollTo(field);
          await tester.enterText(field, _server!.replaceFirst('http://', ''));
          await tapText('Lưu & kiểm tra');
          await wait(
            () => !find
                .descendant(
                  of: find.byType(ServerSettingsCard),
                  matching: find.byType(CircularProgressIndicator),
                )
                .evaluate()
                .isNotEmpty,
            realMs: 10,
          );
          await shot('may-chu-tach-giong');
          await toHome();
          await tapOn(find.text('Bai hat thu').first);
          await wait(() => find.byTooltip('Hạ tone').evaluate().isNotEmpty);
          await idle(const Duration(seconds: 1));
          await songLoaded();
          await tapTip('Thêm');
          await idle(const Duration(milliseconds: 500));
          await tapOn(find.widgetWithText(ListTile, 'Tách giọng ca sĩ'));
          await idle(const Duration(milliseconds: 800));
          await shot('tach-giong-bang');
          await tapOn(
            find.descendant(
              of: find.byType(SeparationPanel),
              matching: find.widgetWithText(FilledButton, 'Tách giọng ca sĩ'),
            ),
          );
          await wait(() => shows('Gửi bài để tách giọng?'));
          await shot('tach-giong-hoi-dong-y');
          await tapText('Đồng ý');
          final sepStart = DateTime.now();
          await idle(const Duration(seconds: 3));
          await shot('tach-giong-dang-tach');
          // Chuyển sang app khác một lúc rồi quay lại (iOS cho chạy nền ngắn).
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.inactive,
          );
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.hidden,
          );
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.paused,
          );
          await idle(const Duration(seconds: 5));
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.hidden,
          );
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.inactive,
          );
          tester.binding.handleAppLifecycleStateChanged(
            AppLifecycleState.resumed,
          );
          await wait(
            () =>
                shows('Giữ giọng ca sĩ gốc') ||
                find.byType(ErrorText).evaluate().isNotEmpty,
            max: const Duration(minutes: 20),
            realMs: 15,
            reason: 'tách giọng chưa xong',
          );
          notes.add(
            'Tách giọng (server thật, đồng hồ thật): '
            '${DateTime.now().difference(sepStart).inSeconds} s',
          );
          await shot('tach-giong-xong');
          await slide('Giữ giọng ca sĩ gốc', 0.3);
          await shot('tach-giong-giu-giong-30');
          await toHome();
        }

        expect(TestLog.enabled, isTrue);
        if (out != null) {
          File('$out/ghi-chu.txt').writeAsStringSync(
            'Thời gian thật trên Mac chạy test (không phải iPhone):\n'
            '${notes.join('\n')}\n',
          );
          await idle(const Duration(milliseconds: 500)); // ghi nốt nhật ký
          TestLog.file.copySync('$out/nhat-ky-thu-nghiem.jsonl');
        }
        // ignore: avoid_print
        print('${device.name}:\n${notes.join('\n')}');
        await tester.pumpWidget(const SizedBox());
        for (var i = 0; i < 10; i++) {
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 5)),
          );
          await tester.pump(const Duration(milliseconds: 20));
        }
        await tester.runAsync(service.shutdown);
        await tester.runAsync(LibraryDb.closeAll);
      },
      skip: skip != null,
      variant: TargetPlatformVariant.only(TargetPlatform.iOS),
      timeout: const Timeout(Duration(minutes: 15)),
    );
  }
}
