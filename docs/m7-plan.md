# M7 – Thu âm đa track, render lại, xuất video 9:16, chia sẻ

> Kế hoạch đã duyệt 2026-11-02 (điểm 1–4 chọn theo đề nghị; điểm 5–8 làm theo đề nghị). Điều kiện hoàn thành (CLAUDE.md mục 9): **video xuất khớp
> tiếng, đăng được lên TikTok**. Spec liên quan: mục 3.8 (thu âm), 3.9 (`tc_record_start/stop`),
> mục 5 màn 3 (nút thu âm / quay video), màn 4 (Nghe lại & chỉnh), màn 5 (Xuất), mục 11 (xuất
> video bằng API native, không ffmpeg; iPhone trước).

## Tiến độ

- [ ] B1: engine 0.8 — bộ thu 3 track, đồng hồ máy, `tc_record_*`.
- [ ] B2: bù trễ theo đường ra + đo click trên iPhone.
- [ ] B3: Nghe lại (`tc_load_take`), render offline, −14 LUFS, AAC.
- [ ] B4: binding 0.8 + app (nút Thu, kho bản thu, Nghe lại & chỉnh, xuất M4A, chia sẻ / lưu).
- [ ] B5: iOS video sóng nhạc.
- [ ] B6: iOS camera trước + đo vỗ tay.
- [ ] B7: nghiệm thu iPhone, đăng TikTok, báo cáo M7.

## Các điểm cần quyết (2026-11-02: duyệt theo đề nghị)

1. **Camera trước có làm trong M7 không.**
   - Spec màn 5: video 9:16 "camera trước **hoặc** nền sóng nhạc + tên bài".
   - Đề nghị: **làm cả hai, chỉ trên iPhone**. Video nền sóng nhạc làm trước (B5) vì khớp tiếng
     chắc chắn (hình dựng từ chính file âm thanh). Camera làm sau (B6) bằng AVFoundation native
     (`AVCaptureSession`, không dùng plugin `camera`). Lý do:
     - plugin có thể đụng `AVAudioSession` của engine;
     - plugin không cho biết chính xác lúc khung hình đầu tiên được chụp, nên không căn tiếng được
       tới vài chục ms.
   - Cách khác: M7 chỉ làm video sóng nhạc, camera để M8. M7 xong sớm khoảng 1 tuần.
2. **Android trong M7.**
   - Theo thứ tự nền tảng đã chốt (2026-10-21), đề nghị **nghiệm thu trên iPhone**.
   - Phần thu âm và render là C++ chung nên Android có luôn. Bộ nén AAC Android (NDK `AMediaCodec`
     + `AMediaMuxer`) được viết và build trong CI, nhưng chưa thử trên máy thật.
   - Xuất video Android (MediaCodec + MediaMuxer + Camera2) để tới giai đoạn Android. Trên Android,
     nút "Xuất video" báo "sắp có".
3. **Chia sẻ lên TikTok / Facebook / Zalo.**
   - Đề nghị: **bảng chia sẻ của hệ thống** (iOS `UIActivityViewController`) cộng nút **"Lưu vào
     Ảnh"**. TikTok, Facebook, Zalo đã cài trên máy sẽ tự hiện trong bảng chia sẻ.
   - Không dùng TikTok Share Kit SDK: phải đăng ký tài khoản nhà phát triển TikTok, chịu điều khoản
     riêng (không phải mã nguồn mở) và qua duyệt app. Có thể thêm sau khi phát hành.
   - Nghiệm thu: bạn đăng thử 1 video lên TikTok (để chế độ "Chỉ mình tôi" cũng được) và chụp màn
     hình.
4. **Bản thu lưu dạng gì.**
   - Spec mục 3.8: ghi giọng khô (dry), giọng đã xử lý (wet) và beat, WAV 48 kHz. Đề nghị:
     - giọng khô: mono, 32-bit float, để render lại không mất chất lượng;
     - giọng đã xử lý: stereo, 16-bit;
     - beat (đúng như đang nghe: tone đã đổi, giọng ca sĩ gốc theo thanh kéo): stereo, 16-bit.
   - Dung lượng khoảng **35 MB / phút**, bài 4 phút ≈ 140 MB. App hiện dung lượng từng bản thu và
     cho xoá. Xuất xong rồi thì có thể xoá bản thu, chỉ giữ video / M4A.
   - Nén FLAC bản thu (bộ nén đã có từ M6) để sau, nếu máy chật.
5. **Bù độ trễ áp lúc đọc, không áp lúc ghi.**
   - File ghi thô, độ lệch cần bù ghi vào `take.json`. Khi nghe lại và render thì giọng được dịch
     sớm lên.
   - Nhờ vậy sửa được về sau: đo lại độ trễ, hoặc bạn tự chỉnh nút **"Giọng sớm / muộn ±"**
     (bước 5 ms) ở màn Nghe lại.
   - Độ lệch được chọn theo thứ tự:
     1. độ trễ đã đo (`tc_measure_latency`) cho đúng loại đường ra đang dùng;
     2. nếu chưa đo: độ trễ hệ điều hành báo cộng với phần chênh đo được ở loa ngoài;
     3. Bluetooth: chỉ có số hệ điều hành báo (sai ±30 ms), nên app nhắc dùng tai nghe có dây.
6. **Khi đang thu.**
   - Khoá nút tone ± (cả bản thu dùng một tone).
   - Vẫn cho đổi preset hiệu ứng: giọng đã xử lý ghi lại đúng như bạn nghe. Khi render lại thì cả
     bài dùng một preset.
   - Thu từ vị trí đang phát. Có đếm ngược 3 giây nếu đang dừng.
7. **Nghe lại & chỉnh nghe ngay, không chờ render.**
   - Ở màn Nghe lại, engine phát beat đã thu, còn giọng khô từ file đi qua đúng chuỗi chỉnh giọng
     + FX như khi hát (thay cho micro). Đổi preset / mức chỉnh giọng là nghe ngay, dùng lại cơ chế
     đổi không click của M5.
   - Chỉ khi bấm "Xuất" mới render offline ra file.
8. **Âm lượng file xuất.**
   - Đề nghị: chuẩn hoá **−14 LUFS**, đỉnh thật (true-peak) ≤ −1 dBTP. TikTok / YouTube / Facebook
     đều hạ hoặc nâng về quanh mức này; file to quá sẽ bị họ nén thêm.
   - Có công tắc tắt chuẩn hoá.

---

## 1. Kết quả đầu ra

- **Engine 0.8** (C API thêm, không đổi hàm cũ):
  - `tc_record_start(engine, const tc_record_config*)`, `tc_record_stop`, `tc_record_get_status`
    (đang thu, số frame đã ghi, **số frame rơi**, vị trí beat lúc bắt đầu, đồng hồ máy lúc bắt đầu,
    độ trễ ra). Ghi 3 file WAV qua luồng ghi riêng.
  - `tc_load_take` (chế độ Nghe lại: beat đã thu + giọng khô qua chuỗi giọng, độ lệch, cắt đầu /
    đuôi).
  - `tc_renderer_*` (render offline kiểu kéo như `tc_exporter`):
    - vào: giọng khô, beat, preset `tc_fx_preset`, mức chỉnh giọng, tone, âm lượng giọng / nhạc,
      cắt đầu / đuôi, độ lệch;
    - ra: WAV hoặc **M4A AAC 48 kHz 192 kbit/s**;
    - chuẩn hoá −14 LUFS, limiter true-peak.
  - Bộ nén AAC dùng codec của hệ điều hành: Apple `ExtAudioFile`, Android NDK
    `AMediaCodec` + `AMediaMuxer`.
- **Binding 0.8** + app:
  - Phòng thử có nút **Thu** (đồng hồ, khoá tone, tuỳ chọn quay camera).
  - Màn **Nghe lại & chỉnh**: phát / tua; đổi preset và mức chỉnh giọng; to nhỏ giọng / nhạc;
    "Giọng sớm / muộn ±"; cắt đầu / đuôi trên dạng sóng.
  - Màn **Xuất**: video camera / video sóng nhạc / chỉ âm thanh, có tiến độ, rồi **Chia sẻ** /
    **Lưu vào Ảnh**.
  - Trang chủ có mục **"Bản thu của tôi"**.
- **Native iOS** (Swift trong `app/ios/Runner/`, kênh `chuantone/media`), không thêm thư viện:
  - dựng video sóng nhạc (CoreGraphics → `AVAssetWriter`, H.264 1080×1920 30 fps);
  - camera trước (`AVCaptureSession`, xem trước bằng platform view, ghi video không tiếng, mốc thời
    gian theo đồng hồ máy);
  - ghép hình + tiếng (`AVMutableComposition` + `AVAssetExportSession`, không nén lại hình);
  - lưu vào Ảnh (`PHPhotoLibrary`, quyền "chỉ thêm"); chia sẻ (`UIActivityViewController`).
- **Tài liệu:** `docs/m7-plan.md`, `docs/record-results.md` (số đo), ADR-007 (thu âm, bù trễ, đồng
  bộ hình – tiếng), `docs/references.md` (BS.1770 true-peak, ITU-R BT.1359, tài liệu AVFoundation).
  `docs/licenses.md`: không thêm thư viện, chỉ dùng framework của hệ điều hành.

---

## 2. Thu âm trong engine

```
audio thread (mỗi block):  mic thô ─┬─ (bản sao) → giọng khô ─┐
                                    └→ chỉnh giọng → FX → giọng đã xử lý (L, R) ─┤→ ring SPSC → luồng ghi → dry.wav / wet.wav / beat.wav
                           beat đang phát (L, R) ──────────────────────────────┘
```

- Audio thread chỉ chép vào ring SPSC cấp phát sẵn lúc `tc_record_start`: khoảng 4 s, 5 kênh
  float, ≈ 3,8 MB. Không cấp phát, không khoá, không I/O. Ring đầy thì đếm frame rơi và đánh dấu
  bản thu lỗi; không bao giờ chặn audio thread.
- Luồng ghi rút ring mỗi ~20 ms, ghi WAV qua tên tạm (`.part`). Khi dừng thì sửa header rồi đổi
  tên. Dữ liệu ghi sau cùng là `take.json`: bài, vị trí bắt đầu, tone, preset, key, tần số, độ lệch,
  đồng hồ máy.
- Ghi đúng **beat trước limiter** và giọng **trước khi trộn**, để còn trộn lại.
- **Căn giọng với beat** (bù khi đọc):
  - Gọi R là độ trễ khứ hồi đo được, L là độ nhìn trước của limiter đầu ra (1 ms), P là độ trễ
    khối chỉnh giọng (10 ms).
  - Beat ở vị trí m được nghe sau L + độ trễ ra; giọng hát theo nó vào micro sau thêm độ trễ vào.
  - Vậy giọng khô ở frame n khớp beat ở frame n − (R + L), còn giọng đã xử lý khớp ở
    n − (R + L + P).
- **Đồng hồ máy** (cho camera): backend RemoteIO chuyển `mHostTime` của mỗi callback vào engine.
  Lúc bắt đầu thu, engine lưu đồng hồ máy của frame đầu tiên và độ trễ ra. App tính được "beat
  frame k được nghe lúc nào" để căn với mốc thời gian của khung hình camera.

## 3. Nghe lại, render, xuất

- **Nghe lại** (`tc_load_take`):
  - Beat đã thu đi vào BeatPlayer. Giọng khô đi vào một "làn giọng" (dùng lại làn stem của M6),
    nhưng thay micro đi qua chỉnh giọng + FX.
  - Làn giọng đọc sớm P frame để bù độ trễ chỉnh giọng.
  - Trong lúc này micro không vào chuỗi giọng.
- **Render offline** (luồng nền):
  - Chạy các lớp `PitchCorrector` / `FxRack` hiện có trên giọng khô đã căn.
  - Trộn với beat (âm lượng giọng / nhạc), đo loudness (`key/loudness`, BS.1770), chỉnh gain về
    −14 LUFS, rồi limiter true-peak (lấy mẫu ×4) ≤ −1 dBTP.
  - Ghi WAV / M4A. Cắt đầu / đuôi có fade 10 ms.
- **Video sóng nhạc**:
  - Native đọc file M4A, tính mức âm 30 lần / giây, vẽ từng khung: nền màu theo thể loại, tên bài,
    tên người hát (tuỳ chọn), sóng nhạc, thanh thời gian.
  - `AVAssetWriter` H.264 dựng video không tiếng, rồi ghép với M4A.
- **Video camera**:
  - Khi thu có bật camera: `AVCaptureSession` camera trước 1080p30, không lấy tiếng từ camera,
    `automaticallyConfiguresApplicationAudioSession = false` để không đụng session của engine.
  - Ghi H.264 bằng `AVAssetWriter` với mốc thời gian gốc của từng khung (đồng hồ máy).
  - Lúc xuất, ghép hình vào timeline của file tiếng với độ lệch:
    (đồng hồ máy của khung đầu) − (lúc beat frame bắt đầu được nghe). Không nén lại hình nên ghép
    nhanh.
  - Hình lật gương như lúc xem trước (giống các app karaoke).
- File xuất: MP4 (H.264 High, 1080×1920, 30 fps; AAC 48 kHz 192 kbit/s stereo) — định dạng TikTok,
  Facebook, Zalo đều nhận.

---

## 4. Chỉ tiêu và cách đo

| Mục | Chỉ tiêu | Cách đo |
|---|---|---|
| Giọng khớp beat sau bù trễ | lệch ≤ 5 ms (loa ngoài, tai nghe có dây) | iPhone: thu khi phát beat có tiếng click qua loa, micro thu lại → tìm độ lệch click trong giọng khô so với beat (công cụ trong Phòng thử) |
| Hình khớp tiếng (video camera) | lệch ≤ 40 ms (ITU-R BT.1359: khó nhận ra trong khoảng +45 / −125 ms) | vỗ tay trước camera khi thu → trên Mac đọc MP4 bằng AVFoundation, so khung hai tay chạm với đỉnh tiếng vỗ |
| Không rơi mẫu | 0 frame rơi, 0 xrun khi thu 10 phút (có / không camera) | `tc_record_get_status`, `tc_get_metrics` |
| CPU thêm khi thu | ≤ 3% một nhân (không camera) | Thông số kỹ thuật trên iPhone |
| Render lại bài 4 phút (M4A) | ≤ 10 s | iPhone |
| Xuất video sóng nhạc bài 4 phút | ≤ 60 s | iPhone |
| Ghép video camera bài 4 phút | ≤ 15 s | iPhone |
| Loudness file xuất | −14 ± 0,5 LUFS, ≤ −1 dBTP | engine đo lại file xuất (test + iPhone) |
| Đăng TikTok | đăng được, tiếng khớp hình | bạn đăng thử 1 video camera + 1 video sóng nhạc |

Test tự động:
- Engine:
  - thu → WAV đúng từng mẫu so với tín hiệu đưa vào (backend giả);
  - độ lệch R + L (+ P) đúng khi giả lập độ trễ;
  - ring đầy thì đếm rơi, không chặn;
  - không cấp phát trên audio thread khi thu (`RealTime.*`);
  - render offline với cùng preset khớp chuỗi real-time (sai lệch nhỏ);
  - chuẩn hoá −14 LUFS / true-peak;
  - AAC giải mã lại được, đúng độ dài (Apple; Linux bỏ qua);
  - ASan / UBSan / TSan.
- Binding: các API mới.
- App: kho bản thu, màn Nghe lại và Xuất ở bề ngang 320 px, kênh native giả.

---

## 5. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | Camera làm đổi / ngắt audio session của engine (rè, đổi route, mất monitor) | Không thêm đầu vào tiếng cho camera, tắt tự cấu hình session; đo xrun 10 phút có camera; nếu vẫn xung đột thì thu camera ở chế độ thấp hơn (720p) |
| 2 | Hình và tiếng trôi dần trong bài dài | Cả hai theo đồng hồ máy; đồng hồ mẫu âm lệch ~20 ppm → 4 phút ≈ 5 ms; đo ở đầu và cuối bài |
| 3 | Bộ nhớ máy: 140 MB / bản thu | Hiện dung lượng, xoá dễ; FLAC sau nếu cần |
| 4 | CPU / nhiệt khi vừa chỉnh giọng vừa quay 1080p | H.264 phần cứng; đo 10 phút; hạ 720p nếu nóng |
| 5 | Bluetooth: không nghe lại được giọng, độ trễ ước sai | Đúng spec (tắt monitor); nhắc dùng tai nghe có dây; "Giọng sớm / muộn ±" |
| 6 | Bản thu dài 10 phút: RAM khi Nghe lại (beat + giọng nạp vào bộ nhớ ≈ 170 MB) | Đo trên iPhone; nếu căng thì đọc file theo cửa sổ |
| 7 | iOS dừng app khi chuyển nền lúc đang xuất | Giữ màn hình mở + báo (như M6); xuất nền để M8 |

---

## 6. Ngoài phạm vi M7

- Xuất video Android, camera Android.
- Lời bài hát trên video, hiệu ứng / bộ lọc hình, HEVC.
- TikTok Share Kit SDK, đăng thẳng không qua bảng chia sẻ.
- Song ca, chấm điểm, livestream.
- Thư viện bài SQLite (Trang chủ chỉ liệt kê bản thu từ thư mục).
- Xuất nền, sao lưu đám mây.

---

## 7. Các bước

| Bước | Nội dung | Ước (tuần) |
|---|---|---|
| B1 | Engine 0.8: bộ thu (ring SPSC + luồng ghi WAV 3 track + `take.json`), đồng hồ máy từ backend, `tc_record_*`; test (đúng mẫu, rơi frame, không cấp phát, sanitizer) | 0,5 |
| B2 | Bù trễ: chọn độ lệch theo đường ra, công cụ đo click trong Phòng thử; **đo trên iPhone** (loa ngoài, tai nghe có dây) | 0,5 |
| B3 | Engine: chế độ Nghe lại (`tc_load_take`), render offline (`tc_renderer_*`), chuẩn hoá −14 LUFS + true-peak, AAC Apple / Android | 1 |
| B4 | Binding 0.8 + app: nút Thu ở Phòng thử, kho bản thu, màn Nghe lại & chỉnh, xuất M4A, kênh native `chuantone/media` (chia sẻ, lưu) | 1 |
| B5 | iOS: video sóng nhạc + ghép tiếng + Lưu vào Ảnh / Chia sẻ; IPA để bạn thử | 0,5 |
| B6 | iOS: camera trước (xem trước, ghi, mốc đồng hồ máy), ghép theo độ lệch; đo vỗ tay | 1 |
| B7 | Nghiệm thu trên iPhone: số đo bảng mục 4, đăng TikTok, `docs/record-results.md`, báo cáo M7 | 0,5 |

Tổng ≈ 5 tuần (kế hoạch gốc: 1 DSP + 5 mobile). Nếu điểm 1 chọn "camera để M8": bỏ B6, ≈ 4 tuần.

**Cách bạn tự kiểm tra khi xong M7:**
- iPhone, tai nghe có dây: Trang chủ → bài → "Hát" → bấm **Thu** (bật camera) → hát 1 phút →
  Dừng.
- Màn Nghe lại: đổi Bolero → Nhạc trẻ (nghe ngay), kéo giọng / nhạc, cắt đầu đuôi → **Xuất video
  camera** → xem lại trong Ảnh: hình khớp tiếng.
- Chia sẻ → TikTok → đăng ("Chỉ mình tôi" cũng được).
- Xuất thêm video sóng nhạc và file M4A; nghe to nhỏ có đều giữa các bài không.
