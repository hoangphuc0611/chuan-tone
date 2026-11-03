# M8 – UI tiếng Việt hoàn chỉnh, hướng dẫn lần đầu, cài đặt, OTG

> Kế hoạch đã duyệt 2026-11-02: điểm 1, 2, 4 theo đề nghị; điểm 3 "chưa có soundcard" (code ở M8,
> nghiệm thu OTG khi có máy, không chặn việc đóng M8); điểm 5–10 theo đề nghị. Điều kiện hoàn thành (CLAUDE.md mục 9): **test với 5 người
> dùng thật không cần hướng dẫn**. Spec liên quan: mục 1 (người dùng không rành kỹ thuật, nút to, ít
> thuật ngữ), mục 3.1 (soundcard / micro USB OTG, đổi route), mục 5 (6 màn hình + dữ liệu SQLite),
> mục 7 (kích thước app ≤ 80 MB, offline, quyền riêng tư), mục 8 (bài mẫu chỉ dùng nhạc có giấy phép).
>
> M7 vẫn mở: còn chờ thử trên iPhone (B2, B4–B6, báo cáo B7). M8 làm song song; buổi thử nội bộ đầu
> tiên của M8 (B7) có thể gộp luôn phần thử M7.

## Tiến độ

- [~] **B1** (2026-11-02, chờ thử "Mở bằng" trên iPhone): thư viện bài SQLite, nhận file từ app khác.
  - `data/library_db.dart`: CSDL `chuantone.db` (sqflite, SQLite của hệ điều hành; không dùng UPSERT vì
    Android 9 có SQLite 3.22) — bảng `songs` (thư viện + lựa chọn theo bài), `analysis`, `settings`.
    Lần tạo đầu tự chuyển `song_prefs.json`, `analysis_cache.json`, `settings.json` vào rồi đổi tên
    file cũ thành `.migrated`; giá trị sai kiểu thì bỏ qua; giữ thứ tự cũ → mới.
  - `SongPrefs`, `AnalysisCache`, `AppSettings` giữ nguyên API, chạy trên CSDL (màn cũ không phải sửa).
  - `data/song_library.dart`: file bài **chuyển** (không chép thêm) vào `<dữ liệu app>/library/<băm
    khoá>.<đuôi>`; thêm lại cùng bài không nhân đôi; bài có lựa chọn từ trước giữ nguyên lựa chọn;
    mới dùng lên đầu; tìm theo tên bỏ dấu (cả chữ tổ hợp của tên file iOS); đổi tên; xoá bài = xoá
    file + giọng đã tách + kết quả dò + lựa chọn (bản thu giữ); đánh dấu bài mất file.
  - Màn **"Bài hát của tôi"** (Trang chủ có nút mới; Trang chủ đầy đủ ở B3): tìm, mở → Phân tích bài
    (có "Hát"), đổi tên, xoá, thêm bài; trống thì hướng dẫn. Bài chọn ở Trang chủ / Phòng thử cũng vào
    thư viện; mở bài ở Phòng thử thì bài lên đầu.
  - **"Mở bằng ChuẩnTone"**: iOS khai báo kiểu file âm thanh (`CFBundleDocumentTypes`, không mở tại
    chỗ), plugin nhận URL qua scene delegate (cả lúc app được mở lên), chuyển file khỏi
    `Documents/Inbox`; Android intent `VIEW` / `SEND` `audio/*` trong `MainActivity` (build CI). Kênh
    `chuantone/incoming` (`take` + báo `available`); app thêm file vào thư viện rồi mở Phân tích bài.
  - Test app 97 (+16): chuyển dữ liệu cũ, thư viện (thêm / chuyển / trùng / tìm / đổi tên / xoá / mất
    file), màn Bài hát của tôi ở 320 px chữ 130%, nhận file "Mở bằng". Test chạy SQLite của máy qua
    `sqflite_common_ffi` (`test/flutter_test_config.dart`).
- [~] **B2** (2026-11-02, chờ thử trên iPhone ở B7): bộ điều khiển phiên hát dùng chung + Màn hát.
  - `engine/sing_controller.dart` (`SingController`): toàn bộ logic của Phòng thử tách khỏi giao
    diện — nạp bài (kèm giọng đã tách; hỏng thì xoá, nạp bài thường), nhớ tone nhạc / thể loại theo
    bài, dò tone rồi chỉnh giọng theo bài (tone sửa tay thắng kết quả dò), tách giọng, âm lượng, nghe
    lại giọng, thu âm (đếm ngược khi nhạc dừng, **khoá tone khi thu**, tự dừng khi đủ thời lượng) +
    camera, số đo. Đồng hồ cao độ ~30 lần/giây qua `ValueNotifier` riêng (không dựng lại cả màn).
    Phòng thử (1160 → 655 dòng) chỉ còn giao diện, hành vi giữ nguyên.
  - **Màn hát** (`pages/sing_page.dart`, bố cục mục 1): tone chữ to nhất + "theo bài / tone bạn đã
    sửa / chọn tay · nhạc +2", `[ − ] Tone +2 [ + ]`, đồng hồ cao độ gọn, Chỉnh giọng (Tắt / Tự
    nhiên / Ca sĩ / Robot), chip Thể loại cuộn ngang, Vang, Giữ giọng ca sĩ gốc (khi đã tách); thanh
    phát + Lùi 10 giây + Phát + **Thu** ("Dừng · 0:42") + camera luôn ở đáy; cảnh báo Bluetooth / loa
    ngoài. Nút loa: âm lượng nhạc / giọng, nghe lại giọng, lọc tiếng ồn. Menu ⋮: tách giọng ca sĩ,
    sửa tone của bài. Lỗi hiện bằng thanh thông báo.
  - **Mẹo lần đầu** (`widgets/coach_marks.dart`): 3 bong bóng Tone → Chỉnh giọng → Thu (khoét sáng
    chỗ được chỉ, "Bỏ qua" / "Tiếp"), hiện một lần sau khi dò tone xong (cài đặt `tipShown.sing`).
  - Màn Phân tích bài → "Hát" mở Màn hát (Phòng thử vẫn ở Trang chủ tới khi có Cài đặt ở B4).
  - Test app 115 (+18): bộ điều khiển chạy với **engine thật** (backend NULL, `processOffline`) — mở
    bài, nhớ theo bài qua hai phiên, tone sửa tay, thu → lưu → Nghe lại → nạp lại bài, khoá tone,
    camera giả, bài hỏng, bản tách hỏng, không mở được thiết bị; Màn hát ở 320 / 375 px chữ 130%
    (cả lúc đang thu), mẹo chỉ hiện một lần, menu sửa tone, nút loa. CI chạy test app với
    `TONECORE_LIB` (thiếu thì test engine bị bỏ qua khi chạy tay, CI bắt buộc có).
- [~] **B3** (2026-11-03, chờ thử trên iPhone ở B7): lời `.lrc`, bài mẫu, Trang chủ mới.
  - **Bộ đọc LRC tự viết** (`data/lrc.dart`): mốc dòng (phần lẻ 1–3 chữ số), nhiều mốc một dòng
    (điệp khúc), mốc từng chữ `<mm:ss.xx>` (và `[mm:ss.xx]` giữa dòng), thẻ `ti` / `ar` / `offset`,
    BOM, CRLF, UTF-16 có BOM, byte hỏng; không có mốc nào → báo không phải file lời. Tô màu theo số
    ký tự đã hát (không cắt giữa chữ cái và dấu rời); dòng chỉ có mốc dòng tô dần tới câu sau nhưng
    không chậm hơn ~0,35 s / ký tự.
  - **Lời trong thư viện**: chép vào `<dữ liệu app>/library/<băm>.lrc` (cột `lrc_file`), lệch lời
    nhớ theo bài (`lrc_offset_ms`), xoá bài xoá cả lời. Nạp lời: chọn kèm file `.lrc` khi thêm bài
    (ghép theo tên, hoặc một nhạc + một lời), "Mở bằng" gửi nhạc + lời cùng lúc / gửi riêng file lời
    (gắn cho bài cùng tên; iOS khai báo kiểu `.lrc`), hoặc ⋮ → **Lời bài hát** ở Màn hát.
  - **Màn hát**: lời chạy chữ dưới đồng hồ cao độ — dòng đang hát to, phần đã hát tô màu; dòng sau
    mờ; chiều cao cố định (các nút không nhảy khi đổi câu); cập nhật ~30 lần/giây riêng phần lời.
    Bảng "Lời bài hát": chọn / đổi file, "Lời sớm hơn / muộn hơn" mỗi lần 0,5 s (±10 s), bỏ lời.
  - **Bài mẫu**: `tools/samples/make_samples.dart` tạo 2 bài luyện giọng (piano + bass + trống nhẹ
    + giai điệu dẫn, nén AAC 96 kbit/s): "Luyện giọng: Đô Rê Mi" (Đô trưởng, 80 BPM, 58 s, 654 KB) và
    "Luyện giọng: La thứ" (La thứ, 72 BPM, 65 s, 716 KB), kèm lời có mốc từng chữ. Engine dò đúng
    tone + nhịp cả hai (keylab: C 80,0 BPM; Am 72,0 BPM; một tone suốt bài). Bấm ở Trang chủ thì
    chép vào thư viện (đánh dấu bài mẫu) rồi vào hát.
  - **Trang chủ mới** (bố cục mục 1): "Thêm bài hát" to nhất, "Hát tiếp" (3 bài mới dùng — bấm là
    vào Màn hát với tone / thể loại / lời như lần trước), "Tất cả bài hát (N)", Bài mẫu, "Bản thu của
    tôi (N)", ⚙. Tự cập nhật khi quay về từ màn khác. Thẻ "engine OK" chuyển vào **Cài đặt** (khung
    đầu: Đo độ trễ, Phòng thử ở Nâng cao, Giới thiệu); Trang chủ chỉ báo khi engine lỗi. "Bài hát của
    tôi" bấm bài → Màn hát; dòng phụ thêm "có lời".
  - Test app 143 (+28): bộ đọc LRC, lời trong thư viện, ghép file lời, bài mẫu (asset thật), lời
    chạy chữ (tô màu, lệch lời, chiều cao cố định ở 320 px chữ 130%), bộ điều khiển (lời, vị trí phát,
    lệch lời nhớ theo bài), Màn hát có lời + bảng Lời bài hát, Trang chủ (Hát tiếp, bài mẫu, Cài đặt,
    engine lỗi, 320 px chữ 130%).
- [~] **B4** (2026-11-03, chờ thử trên iPhone ở B7): engine 0.9 + màn Cài đặt.
  - **Engine 0.9** (API 0.9.0): `tc_metrics.input_peak_db` (đỉnh micro thô ~300 ms, hai cửa sổ nối
    nhau, đọc qua atomic, không cấp phát), `input_clips` (số đợt mẫu ≥ −0,01 dBFS), `input_source` /
    `input_preference`; `tc_set_input_preference(engine, TC_INPUT_AUTO | BUILTIN | HEADSET | USB,
    device_id)`. Lựa chọn giữ cả khi chưa start, báo cho backend trước khi mở stream. iOS: tìm cổng
    trong `availableInputs` rồi `setPreferredInput` (không giữ mutex khi đặt — đặt cổng sinh thông
    báo đổi route), đặt lại mỗi lần đổi route (cắm soundcard sau vẫn được chọn), micro đang dùng đọc
    từ `currentRoute.inputs`. Android: plugin Java tìm `AudioDeviceInfo` theo loại → `setDeviceId`
    cho stream micro, đổi khi đang chạy thì mở lại stream, mở không được thì để hệ điều hành chọn;
    `RouteMonitor` tìm lại id khi cắm / rút. Backend giả cho test qua `setBackendFactoryForTesting`.
    Test engine 305 (+5: đỉnh / suy giảm, đếm đợt chạm 0 dBFS qua ranh giới khối, struct v0.8 vẫn
    nhận, kiểm tra lựa chọn, lệnh tới backend giả trước / sau start).
  - **Binding**: `InputKind`, `EngineMetrics.inputPeakDb / inputClips / inputSource /
    inputPreference`, `setInputPreference`, `selectInput` (Android hỏi plugin id thiết bị), test 28.
  - **Cài đặt** (bố cục mục 1): *Âm thanh* — **Đo độ trễ từng bước** (rút tai nghe, có hiện đường
    tiếng đang dùng → đo qua loa → kết quả bằng lời: "Máy bạn hát mượt" ≤ 35 ms / "Hơi trễ, nên dùng
    tai nghe có dây" ≤ 60 ms / "Trễ nhiều"; lưu số đo để căn giọng khi thu), **Micro** (4 lựa chọn,
    micro đang dùng, thanh mức micro "nói a lô", cảnh báo vỡ tiếng), "Tự bật nghe lại giọng khi cắm
    tai nghe" (tắt khi về loa ngoài để không hú; thay cho "nghe lại giọng mặc định bật / tắt");
    *Xuất* — chất lượng âm thanh 128 / 192 / 256 kbit/s, video 720p (plugin xuất bằng preset
    1280×720 thay vì giữ nguyên), cân bằng âm lượng −14 LUFS bật / tắt — màn Nghe lại dùng ngay;
    *Bộ nhớ* — dung lượng bài hát / giọng đã tách / bản thu / file đã xuất, xoá từng nhóm (có hỏi
    lại); *Trợ giúp* — mẹo hát, xem lại bong bóng mẹo ở Màn hát; *Giới thiệu* — phiên bản app +
    engine, quyền riêng tư, giấy phép thư viện (`showLicensePage` + Oboe Apache-2.0); *Nâng cao* —
    Phòng thử, đo độ trễ kỹ thuật, máy chủ tách giọng, trạng thái engine. "Chế độ soundcard" để B6;
    "Xem lại hướng dẫn" để B5.
  - Sửa kèm: ô máy chủ tách giọng tràn ngang ở 320 px chữ 130%; bấm liên tiếp nhiều lựa chọn xuất
    chỉ lưu lựa chọn cuối.
  - Test app 154 (+11): cài đặt mới, bộ nhớ, đánh giá độ trễ + dòng CSV, trình đo từng bước, trang
    Cài đặt (lưu lựa chọn, xoá có hỏi lại, 320 px chữ 130%), tự nghe lại giọng theo đường tiếng,
    micro đã chọn tới engine, trang Micro (thanh mức, cảnh báo vỡ tiếng) với engine thật.
- [ ] B5 — Hướng dẫn lần đầu, Việt hoá phần còn lại, icon, thông báo lỗi
- [ ] B6 — Chế độ soundcard (OTG)
- [ ] B7 — Chạy nền ngắn, nhật ký thử nghiệm, thử nội bộ 1–2 người
- [ ] B8 — Thử với 5 người dùng → sửa → báo cáo M8

---

## Hiện trạng sau M7 (cái gì đã có, cái gì còn thiếu)

| Phần | Đã có | Còn thiếu cho người dùng thật |
|---|---|---|
| Trang chủ | 4 nút to (Thêm bài hát, Phòng thử, Bản thu của tôi, Đo độ trễ), thẻ "engine OK" | Bài gần đây, bài mẫu, nút Cài đặt; thẻ engine là thông tin kỹ thuật |
| Thêm bài | Chọn file qua app Tệp → màn Phân tích bài (M6) | iOS chép file vào thư mục tạm, có thể bị xoá → không mở lại được bài cũ; chưa nhận file từ Zalo / Safari |
| Màn hát | Phòng thử: một danh sách dài gồm mọi thứ (beat, thu, camera, tone, tách giọng, âm lượng, nghe lại, chỉnh giọng, FX, thông số) | Màn hát theo spec mục 5 màn 3: tone thật to, 3 nút chỉnh giọng, tone ±, chip thể loại, "Vang", lời chạy chữ, đồng hồ cao độ, nút thu |
| Nghe lại & Xuất | Có (M7) | Giữ nguyên, chỉ sửa chữ / bố cục cho đồng bộ |
| Đo độ trễ | Trang kỹ thuật (lịch sử, CSV, kiểm tra khớp tiếng) | Dạng hướng dẫn từng bước cho người thường |
| Cài đặt | Chưa có màn riêng (địa chỉ máy chủ tách giọng nằm trong Phòng thử) | Spec màn 6: đo độ trễ, chọn micro / tai nghe, soundcard OTG, chất lượng xuất |
| Dữ liệu | File JSON: `settings.json`, `song_prefs.json`, bộ nhớ phân tích, `take.json` | Spec: SQLite cho thư viện bài, kết quả phân tích, lựa chọn của người dùng |
| Chữ hệ thống | Chữ của app là tiếng Việt | Chữ có sẵn của Flutter (Quay lại, Huỷ, chọn chữ…) vẫn là tiếng Anh; icon app là icon mặc định của Flutter |
| Việc dài | Giữ màn hình mở khi tách giọng / xuất | Chuyển app ra nền là hỏng việc (M6, M7 hẹn sang M8) |
| OTG | Engine nhận ra route USB (`TC_ROUTE_USB`) | Chưa có chế độ soundcard, chưa xử lý soundcard bật loopback |

---

## Các điểm cần quyết (2026-11-02: đã duyệt)

1. **Lưu thư viện bài bằng gì.**
   - Spec mục 5: "SQLite cục bộ lưu thư viện bài, kết quả phân tích, preset người dùng".
   - Đề nghị: **SQLite qua gói `sqflite`** (BSD-2-Clause). Gói này dùng SQLite có sẵn của hệ
     điều hành (iOS, Android), không đóng gói thêm thư viện C. Test trên Mac / CI dùng
     `sqflite_common_ffi` (BSD-2) + `sqlite3` (MIT), chỉ ở dev.
   - Lần đầu mở bản mới, app tự chuyển `song_prefs.json` và bộ nhớ phân tích vào SQLite một lần.
     File cũ được đổi tên, không xoá. `take.json` và `meta.json` của giọng đã tách vẫn nằm cạnh
     file của chúng, để thư mục bản thu đứng riêng được.
   - Kiểm trước khi chốt: `sqflite` phải build được với bộ build Android hiện tại (AGP 9.1). Bài học
     cũ: `shared_preferences` hỏng ở bản release, `permission_handler` đòi SDK 37. Nếu không build
     được thì quay về JSON và báo bạn.
   - Cách khác: giữ JSON (không thêm gói nào). Vài trăm bài vẫn đủ nhanh, nhưng lệch spec, và về
     sau làm kho tone / fingerprint sẽ phải chuyển.
2. **Bài mẫu** (spec màn 1; mục 8: chỉ nhạc royalty-free hoặc nhạc bạn cung cấp kèm giấy phép).
   - Beat tự làm của một bài hát có bản quyền vẫn vướng quyền tác giả phần nhạc và lời, nên không
     dùng được.
   - Đề nghị: **mình tự tạo 1–2 bài luyện giọng bằng code** (hợp âm piano tổng hợp + trống nhẹ,
     kèm lời `.lrc` "Đô – Rê – Mi…"). Bài này là của mình nên không vướng bản quyền, và vẫn đủ để
     thử dò tone, chỉnh giọng, lời chạy chữ, thu âm ngay lần đầu mở app. Script tạo bài nằm trong
     `tools/samples/`.
   - Khi nào bạn có beat có giấy phép (mua royalty-free hoặc tự sản xuất bài không còn bản quyền)
     thì gửi kèm giấy phép, mình thêm vào app.
   - Cách khác: (a) chỉ dùng beat bạn cung cấp; (b) chưa có bài mẫu ở M8.
3. **Soundcard OTG: thử trên máy nào.**
   - Phần chung cho mọi máy: nhận ra soundcard, chế độ soundcard, đo độ trễ qua loopback, cảnh báo
     nhạc lọt vào micro (chi tiết ở mục 4).
   - Cần một soundcard thật (vd. K10, H9, ICON Upod… loại có cổng OTG / Lightning) để thử trên
     iPhone. Android: code nhận USB build trong CI, chỉ thử máy thật ở giai đoạn Android.
   - Nếu chưa có soundcard thì vẫn code theo tài liệu, và để phần nghiệm thu OTG tới khi có máy.
   - **Đã chọn: chưa có soundcard.** B6 code theo tài liệu và test bằng tín hiệu giả. Nghiệm thu
     trên soundcard thật làm khi có máy, không chặn việc đóng M8.
4. **Tổ chức buổi thử 5 người.**
   - Đề nghị: **chỉ trên iPhone** (theo thứ tự nền tảng đã chốt). Dùng máy của bạn, hoặc iPhone
     khác cài qua Sideloadly. Ai cũng dùng tai nghe có dây.
   - Thử theo hai vòng:
     - vòng nhỏ 1–2 người (B7) để bắt lỗi lớn;
     - sửa xong mới thử 5 người chính thức (B8).
   - Bạn là người điều phối. Mình soạn kịch bản, phiếu ghi và phiếu SUS tiếng Việt (mục 7).
   - **Tiêu chí đạt:**
     - mỗi nhiệm vụ có ≥ 4/5 người tự làm xong; nhiệm vụ "thêm bài rồi hát có chỉnh giọng" phải
       5/5;
     - không có lỗi chặn (crash, mất bản thu);
     - điểm SUS trung bình ≥ 70.
   - Cách khác: thử cả iPhone và Android. Cách này phải làm giai đoạn Android trước khi đóng M8.

Các điểm dưới đây sẽ làm theo đề nghị nếu bạn không có ý khác:

5. **Lời bài hát `.lrc`** (spec màn 3). Làm trong M8.
   - Nạp lời: khi thêm bài, chọn thêm file `.lrc`; hoặc app tự ghép nếu có file `.lrc` cùng tên.
   - Màn hát hiện 2–3 dòng, dòng đang hát tô màu theo thời gian. Hỗ trợ cả LRC có mốc từng chữ.
     Có chỉnh lệch lời ± 0,5 s.
   - Bộ đọc LRC tự viết. App không kèm lời bài nào; người dùng tự nạp lời của họ.
6. **Màn hát là màn chính; Phòng thử chuyển vào Cài đặt → Nâng cao.**
   - Logic của Phòng thử (1160 dòng) tách ra thành một bộ điều khiển phiên hát có test. Màn hát và
     Phòng thử dùng chung bộ điều khiển này, không chép code hai nơi.
7. **Engine 0.9 (việc nhỏ, ~0,5 tuần DSP):**
   - `tc_metrics.input_peak_db` và `input_clips`: thanh mức micro ở hướng dẫn lần đầu, cảnh báo
     soundcard để gain quá to;
   - `tc_set_input_preference(engine, AUTO | BUILTIN | HEADSET | USB)` cho mục "Micro" trong Cài
     đặt. iOS dùng `AVAudioSession.setPreferredInput`. Android: Kotlin tìm thiết bị theo loại rồi
     đưa `deviceId` cho Oboe (build trong CI).
8. **Chạy nền:** dùng `beginBackgroundTask` (iOS cho thêm khoảng 30 giây) khi đang xuất hoặc upload
   tách giọng, và tự chạy tiếp khi quay lại app:
   - mã job tách giọng được lưu, nên quay lại app thì hỏi tiếp server, không phải tách lại;
   - xuất bị ngắt thì tự xuất lại.
   - Chạy nền hoàn toàn (`URLSession` nền, `BGProcessingTask`) để M9, nếu đo thấy cần.
9. **Nhận file từ app khác:** khai báo kiểu file âm thanh để ChuẩnTone hiện trong bảng chia sẻ /
   "Mở bằng" của Zalo, Tệp, Safari, AirDrop. Người không rành rất khó tự tìm file trong app Tệp.
   - iOS: `CFBundleDocumentTypes`, nhận URL ở `SceneDelegate`.
   - Android: intent `VIEW` / `SEND` `audio/*` (build CI).
10. **Không làm fingerprint ở M8.** Fingerprint chỉ cần cho kho tone dùng chung (giai đoạn sau).
    Khoá bài vẫn là tên file + kích thước như hiện nay.

---

## 1. Kết quả đầu ra

### Luồng chính

```
Lần đầu mở app → Hướng dẫn (micro, đo nhanh, tai nghe, thử giọng) → Trang chủ
Trang chủ ─ Thêm bài hát (Tệp / "Mở bằng" từ Zalo, Safari…) → Phân tích bài → Hát ─┐
          ─ Bài gần đây / Bài mẫu ────────────────────────────────────────→ Màn hát
Màn hát ─ Thu → Dừng → Nghe lại & chỉnh → Xuất video / âm thanh → Lưu vào Ảnh / Chia sẻ
Trang chủ ─ ⚙ Cài đặt (đo độ trễ, micro, soundcard, chất lượng xuất, bộ nhớ, giới thiệu, nâng cao)
```

### Trang chủ

```
┌──────────────────────────────────┐
│ ChuẩnTone                     ⚙  │
│ Bấm bài là hát                   │
│ ┌──────────────────────────────┐ │
│ │ ＋  Thêm bài hát              │ │  nút to nhất
│ │     Từ Tệp, Zalo, Safari…    │ │
│ └──────────────────────────────┘ │
│ Hát tiếp                         │
│   Duyên Phận    Sol thứ · +2   ▶ │  bấm là vào Màn hát với tone /
│   Lạc Trôi      La thứ         ▶ │  thể loại / lời như lần trước
│   Tất cả bài hát (12)          › │
│ Bài mẫu                          │
│   Luyện giọng: Đô Rê Mi        ▶ │
│ [ Bản thu của tôi (5) ]          │
└──────────────────────────────────┘
```

- **Tất cả bài hát:** tìm theo tên, đổi tên, xoá bài. Xoá bài là xoá file nhạc, giọng đã tách và
  kết quả phân tích; bản thu vẫn giữ. Mỗi bài hiện tone, BPM, "đã tách giọng".
- Thẻ "engine OK" chuyển vào Cài đặt → Giới thiệu. Trang chủ chỉ báo khi engine lỗi.

### Màn hát

```
┌──────────────────────────────────┐
│ ←  Duyên Phận            🔊   ⋮  │  🔊 âm lượng giọng / nhạc / nghe lại
│          Sol thứ  (Gm)           │  ⋮ tách giọng, sửa tone, nạp lời,
│       theo bài · nhạc +2         │    quay video
│    [ − ]    Tone +2    [ + ]     │
│  ┌ đồng hồ cao độ ─────────────┐ │
│  │  thấp ◀────── ● ──────▶ cao │ │
│  └─────────────────────────────┘ │
│     …một thoáng ta rực rỡ…       │  lời .lrc: dòng đang hát tô màu
│     rồi nhạt nhoà                │
│ Chỉnh giọng                      │
│ [ Tắt ][ Tự nhiên ][ Ca sĩ ][Robot]│
│ (Bolero)(Ballad)(Nhạc trẻ)(Remix)…│  chip thể loại, cuộn ngang
│ Vang ─────────●───               │
│ Giữ giọng ca sĩ ●────            │  chỉ hiện khi bài đã tách giọng
│ 1:02 ━━━━━━●───────────── 4:31   │
│    ⏪ 10s      ▶      ● Thu   📷  │
└──────────────────────────────────┘
```

- Tone hiện là chữ to nhất màn hình. Khi bài chuyển tone giữa chừng, chữ đổi theo vị trí đang phát
  (đã có từ M3).
- Nút tone ± và chip thể loại lưu theo từng bài (đã có).
- Khi đang thu: khoá tone; nút Thu thành "Dừng · 0:42"; nếu đang dùng Bluetooth thì cảnh báo (đã
  có từ M7).
- Lần đầu vào màn này, 3 bong bóng mẹo chỉ vào: **Tone**, **Chỉnh giọng**, **Thu**.
- Máy nhỏ (iPhone SE, rộng 320 / 375 px, chữ hệ thống 130%): không tràn, không cắt chữ. Phần dưới
  cuộn được, còn thanh phát + nút Thu luôn ở đáy màn hình.

### Cài đặt

| Nhóm | Mục |
|---|---|
| Âm thanh | **Đo độ trễ** (từng bước: rút tai nghe → đo qua loa → báo kết quả bằng lời: "Máy bạn hát mượt" / "Hơi trễ, nên dùng tai nghe có dây"), **Micro** (Tự động / Micro của máy / Micro tai nghe / Soundcard), **Chế độ soundcard**, nghe lại giọng mặc định bật / tắt |
| Xuất | Chất lượng âm thanh (Tiêu chuẩn 128 / Cao 192 / Rất cao 256 kbit/s), video 1080p / 720p (720p nhẹ hơn khi gửi Zalo), "Cân bằng âm lượng cho mạng xã hội" (−14 LUFS) bật / tắt |
| Bộ nhớ | Dung lượng của bài hát / giọng đã tách / bản thu / video đã xuất, xoá từng nhóm |
| Trợ giúp | Xem lại hướng dẫn, mẹo hát (cắm tai nghe, chỉnh tone, khi nào dùng Robot) |
| Giới thiệu | Phiên bản app / engine, giấy phép các thư viện có trong app (`showLicensePage` + Oboe, file_picker… theo `docs/licenses.md`), quyền riêng tư (khi nào âm thanh được gửi đi) |
| Nâng cao | Phòng thử, thông số kỹ thuật (CPU, xrun, độ trễ), địa chỉ máy chủ tách giọng, kiểm tra khớp tiếng khi thu, chế độ thử nghiệm (ghi nhật ký) |

### Hướng dẫn lần đầu (bỏ qua được, xem lại được trong Cài đặt)

1. **Chào:** "Bấm bài là hát", kèm 3 ý ngắn: tự dò tone, tự chỉnh giọng, tự chọn hiệu ứng.
2. **Micro:** giải thích vì sao app cần micro, rồi mới hiện hộp hỏi quyền của hệ thống. Nếu người
   dùng từ chối thì có nút mở Cài đặt của iPhone.
3. **Đo nhanh (5 giây):** "Để máy chỗ yên tĩnh, máy sẽ kêu tách tách". Đo qua loa ngoài trước khi
   cắm tai nghe. Kết quả dùng để căn giọng khi thu (M7).
4. **Tai nghe:** hình minh hoạ cắm tai nghe có dây. App nhận ra đường tiếng đang dùng:
   - có dây: ✓;
   - Bluetooth: "chỉ nghe được nhạc, không nghe lại được giọng vì bị trễ";
   - loa ngoài: "vẫn hát được nhưng không nghe lại giọng".
5. **Thử giọng:** "Nói a lô hoặc hát thử". Có thanh mức micro, giọng nghe lại có vang. Hỏi "Nghe
   rõ giọng mình chưa?": nếu bấm Chưa thì hiện mẹo xử lý.
6. **Xong:** hai nút "Thêm bài hát đầu tiên" và "Hát thử bài mẫu".

### Việt hoá và giao diện

- Thêm `flutter_localizations` (thuộc Flutter SDK, BSD-3) với `Locale('vi')`, để chữ có sẵn của
  Flutter (Quay lại, Huỷ, Sao chép, ngày giờ…) thành tiếng Việt. iOS đặt
  `CFBundleDevelopmentRegion = vi`.
- **Bảng thuật ngữ** (phụ lục cuối file): mỗi khái niệm dùng một từ duy nhất trên mọi màn hình.
- **Thông báo lỗi bằng lời thường kèm việc cần làm**, ví dụ "Không kết nối được máy chủ tách giọng
  — kiểm tra Wi-Fi rồi thử lại". Không hiện chữ của exception; chi tiết kỹ thuật để ở nút "Chi
  tiết".
- **Dễ đọc:** chữ thân tối thiểu 16, nút chính cao ≥ 56 px, theo cỡ chữ của hệ thống tới 130%,
  giao diện sáng / tối theo máy.
- **Icon app và màn khởi động ChuẩnTone:** mình vẽ bằng code (vector → PNG các cỡ), bạn duyệt.
- Trên Android, tính năng chỉ có ở iPhone (quay video, xuất video) báo "sắp có", không hiện nút
  hỏng.

---

## 2. Dữ liệu (SQLite)

```
songs(id, key UNIQUE /* tên|kích thước */, title, file /* tương đối trong library/ */, size,
      duration_ms, added_at, last_sung_at, sing_count, sample /* 1 = bài mẫu */, lrc_file,
      lrc_offset_ms, transpose, genre, fx_mix, correction, key_override)
analysis(song_key, engine_version, json, PRIMARY KEY(song_key, engine_version))
settings(key PRIMARY KEY, value)
```

- File nhạc được **chuyển** (đổi chỗ, không chép thêm bản) vào `Application Support/library/`. iOS
  không tự xoá thư mục này như thư mục tạm.
- `StemStore`, `TakeStore` giữ nguyên chỗ và khoá. Bản thu nối với bài qua `songName` / `songSize`
  sẵn có trong `take.json`.
- Chuyển dữ liệu cũ có test: dữ liệu giả của M5–M7 → SQLite → đọc lại đúng từng lựa chọn.

---

## 3. Engine 0.9

- `tc_metrics.input_peak_db`: đỉnh micro trong khoảng 300 ms gần nhất. Audio thread ghi qua
  atomic, không cấp phát.
- `tc_metrics.input_clips`: số lần micro chạm 0 dBFS.
- `tc_set_input_preference(engine, tc_input_pref)`:
  - iOS backend chọn cổng tương ứng (`builtInMic`, `headsetMic`, `usbAudio`) bằng
    `setPreferredInput`, rồi xử lý đổi route như hiện tại;
  - Android nhận `deviceId` từ Kotlin (`AudioManager.getDevices`) rồi mở lại stream (build CI).
- Test:
  - đỉnh / clip đúng trên tín hiệu giả;
  - không cấp phát;
  - lệnh chọn micro tới backend giả;
  - binding '0.9.0'.
- Chưa sửa gì ở DSP. Nếu soundcard thật đưa micro vào một kênh duy nhất (trái hoặc phải) thì thêm
  tuỳ chọn chọn kênh ở B6 (xem rủi ro 4).

---

## 4. Chế độ soundcard (OTG)

Cách nối thường gặp ở Việt Nam: micro → soundcard (có vang / echo riêng) → cáp OTG → điện thoại;
tiếng của điện thoại → soundcard → tai nghe. Soundcard thường tự cho người hát nghe giọng mình
(nghe trực tiếp, không trễ). Nhiều soundcard còn có **loopback**: trộn tiếng điện thoại vào lại micro
để livestream.

- **Nhận ra soundcard:** route USB (iOS có sẵn; Android: Kotlin báo `TYPE_USB_DEVICE` /
  `TYPE_USB_HEADSET`). Cắm vào là app hỏi một lần: "Dùng chế độ soundcard?"
- **Chế độ soundcard:**
  - Tắt nghe lại giọng trong app, vì soundcard đã cho nghe giọng trực tiếp; nếu để cả hai thì nghe
    giọng đôi, lệch nhau.
  - Chọn hiệu ứng: **"Dùng hiệu ứng của soundcard"** (app để preset Khô, có thể tắt chỉnh giọng) hoặc
    **"Dùng hiệu ứng của app"** (chỉ áp vào bản thu và lúc Nghe lại).
- **Đo độ trễ qua loopback:** bật loopback trên soundcard rồi bấm Đo. Tiếng tách đi vòng bằng điện
  (không qua không khí) nên đo rất chính xác. Kết quả lưu theo route USB, dùng để căn bản thu
  (ADR-007).
- **Cảnh báo nhạc lọt vào micro:** nếu loopback vẫn bật khi thu thì giọng khô có lẫn beat, và khi
  render beat bị nhân đôi, lệch nhau.
  - App tìm tương quan giữa `dry.wav` và `beat.wav` quanh độ lệch đã biết (dùng lại phần đo của
    `sync_check.dart`).
  - Thấy lẫn beat thì báo "Soundcard đang trộn nhạc vào micro — tắt Loopback khi thu" ở màn Nghe
    lại, và khi bấm Thu ở chế độ soundcard.
  - Test bằng micro giả có / không lẫn beat.
- Soundcard chỉ chạy 44,1 kHz: iOS RemoteIO và Oboe tự đổi tần số. Sẽ đo xem độ trễ tăng bao
  nhiêu.

---

## 5. Chạy nền ngắn và việc dở

- Kênh `chuantone/media` thêm `beginBackground` / `endBackground`, bọc quanh xuất và upload tách
  giọng.
- `StemStore` lưu job đang chạy (mã job, số đoạn đã gửi). Mở lại app → "Đang tách dở — tiếp tục"
  → hỏi tiếp server. Server còn giữ job 24 giờ; nếu server đã khởi động lại thì báo và cho tách
  lại.
- Xuất bị ngắt: bỏ file dở, quay lại màn Nghe lại thì tự xuất lại (có báo).

---

## 6. Chỉ tiêu và cách đo

| Mục | Chỉ tiêu | Cách đo |
|---|---|---|
| 5 người tự làm được từng nhiệm vụ | ≥ 4/5 mỗi nhiệm vụ; "thêm bài → hát có chỉnh giọng" 5/5 | quan sát, phiếu ghi (mục 7) |
| Điểm SUS trung bình | ≥ 70 ("tốt" theo Bangor và cộng sự, 2009) | phiếu SUS 10 câu tiếng Việt |
| Từ lần đầu mở app tới câu hát đầu tiên | ≤ 3 phút (gồm hướng dẫn, chưa tính tách giọng) | đồng hồ + nhật ký thử nghiệm |
| Lỗi chặn (crash, mất bản thu) | 0 trong mọi buổi thử | nhật ký thử nghiệm |
| Chữ tiếng Anh còn trên giao diện | 0 | rà từng màn; widget test kiểm locale `vi` |
| Máy nhỏ, chữ to | 320 px, chữ 130%: không tràn ở mọi màn | widget test |
| Kích thước IPA | ≤ 80 MB (spec mục 7) | CI |
| Mở app tới Trang chủ | ≤ 2 s trên iPhone 13 Pro | đo |
| Soundcard | nhận ra, đo độ trễ qua loopback, thu không lẫn nhạc | soundcard thật khi có máy (điểm 3, không chặn đóng M8) |

Test tự động:
- Engine: mức micro, chọn micro, sanitizer.
- Binding: API 0.9.
- App:
  - SQLite (thêm / tìm / xoá bài, chuyển dữ liệu cũ);
  - bộ đọc LRC (mốc dòng, mốc từng chữ, lệch, file lỗi);
  - bộ điều khiển phiên hát (giống hành vi Phòng thử hiện tại);
  - phát hiện nhạc lọt vào micro;
  - mọi màn mới ở 320 px + chữ 130%;
  - hướng dẫn lần đầu chỉ hiện một lần;
  - kênh native giả.

---

## 7. Thử với người dùng

**Người thử:** 5 người 18–50 tuổi, không phải bạn hay người trong nhóm:
- ≥ 2 người trên 35 tuổi;
- ≥ 2 người không rành công nghệ;
- ≥ 1 người hát bolero;
- ≥ 1 người hay livestream / đăng TikTok.

**Chuẩn bị:**
- iPhone đã cài bản mới, đã xoá dữ liệu để thấy phần hướng dẫn lần đầu;
- tai nghe có dây;
- 1 bài nhạc gửi sẵn cho người thử qua Zalo hoặc AirDrop;
- máy chủ tách giọng chạy trên Mac cùng Wi-Fi.

**Nhiệm vụ** (giao bằng lời, không chỉ chỗ bấm; người thử vừa làm vừa nói ra điều mình nghĩ):

| # | Nhiệm vụ |
|---|---|
| 1 | Mở app lần đầu, làm theo hướng dẫn tới khi nghe được giọng mình trong tai nghe |
| 2 | Thêm bài vừa nhận qua Zalo và hát thử một đoạn có chỉnh giọng |
| 3 | "Bài này cao quá" → hạ tone cho dễ hát |
| 4 | Đổi sang kiểu Bolero, cho giọng vang hơn |
| 5 | Thu 30 giây, nghe lại, cho nhạc nhỏ đi, xuất video rồi lưu vào Ảnh |
| 6 | Quay về Trang chủ, mở lại bài vừa hát và bản thu vừa làm |

**Ghi lại** (phiếu mình soạn ở `docs/m8-usability.md`):
- tự làm xong / cần gợi ý / bỏ cuộc;
- thời gian;
- chỗ bấm nhầm;
- câu nói đáng chú ý.

Sau buổi thử, người thử điền SUS (Brooke 1996, bản dịch tiếng Việt).

**Nhật ký thử nghiệm:** bật trong Nâng cao. Ghi màn hình đã vào, thao tác chính, lỗi, kèm mốc thời
gian, vào file trên máy (không gửi lên mạng). Xuất bằng bảng chia sẻ → AirDrop sang Mac;
`tools/usability/` tính thời gian từng nhiệm vụ.

---

## 8. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | Người dùng lạc ở bước lấy file nhạc (app Tệp khó dùng) | "Mở bằng ChuẩnTone" từ Zalo / Safari; hướng dẫn có hình; bài mẫu để hát được ngay |
| 2 | Tách giọng cần máy chủ trên Mac cùng Wi-Fi | Buổi thử làm ở chỗ bạn; không có máy chủ thì nút tách ẩn (đã có) |
| 3 | Cài qua Sideloadly: Apple ID miễn phí hết hạn sau 7 ngày, tối đa 3 app | Thử trên iPhone của bạn; cài lại ngay trước buổi thử |
| 4 | Soundcard mỗi loại một kiểu (micro chỉ ở một kênh, chỉ 44,1 kHz, vang soundcard chồng vang app) | Đo trên soundcard thật; thêm chọn kênh micro nếu cần; ghi danh sách soundcard đã thử |
| 5 | Tách Phòng thử (1160 dòng) thành bộ điều khiển dùng chung làm hỏng tính năng cũ | Viết test cho hành vi hiện tại trước khi tách; Phòng thử chạy trên chính bộ điều khiển mới |
| 6 | Chuyển sang SQLite làm mất lựa chọn cũ | Chuyển một lần, giữ file JSON cũ (đổi tên), có test |
| 7 | Người lớn tuổi khó đọc / khó bấm | Chữ ≥ 16, nút ≥ 56 px, chữ hệ thống tới 130%; ưu tiên người > 35 tuổi trong vòng thử nhỏ |
| 8 | iOS dừng app ở nền khi đang xuất / tách | `beginBackgroundTask` + tự tiếp tục (mục 5) |

---

## 9. Ngoài phạm vi M8

- Fingerprint và kho tone dùng chung (giai đoạn sau, đi cùng server kho tone).
- Preset tự chỉnh EQ / compressor. Người dùng chỉ chọn thể loại + "Vang", đúng mục tiêu "không cần
  kiến thức âm thanh".
- Chạy nền hoàn toàn (`URLSession` nền, `BGProcessingTask`) → M9 nếu cần.
- Android: xuất video / camera, thử soundcard và thử người dùng trên Android → giai đoạn Android.
- Dịch vụ báo crash (Crashlytics / Sentry) → M9, cần quyết về quyền riêng tư.
- Lời bài hát trên video xuất, tài khoản, đồng bộ đám mây, chấm điểm, song ca, livestream, ngôn ngữ
  khác ngoài tiếng Việt.

---

## 10. Các bước

| Bước | Nội dung | Ước (tuần) |
|---|---|---|
| B1 | SQLite (`sqflite`) + chuyển dữ liệu cũ; chuyển file bài vào thư viện; "Mở bằng ChuẩnTone" (iOS; Android build CI); trang Tất cả bài hát | 1 |
| B2 | Bộ điều khiển phiên hát (tách khỏi Phòng thử, có test) + Màn hát (bố cục mục 1, âm lượng, tách giọng, quay video, mẹo lần đầu) | 1 |
| B3 | Bộ đọc + hiển thị lời `.lrc`; tạo bài mẫu (`tools/samples/`); Trang chủ mới | 0,5 |
| B4 | Engine 0.9 (mức micro, chọn micro) + binding; màn Cài đặt (đo độ trễ từng bước, micro, chất lượng xuất, bộ nhớ, giới thiệu & giấy phép, nâng cao) | 1 |
| B5 | Hướng dẫn lần đầu; Việt hoá chữ hệ thống; bảng thuật ngữ áp vào mọi màn; thông báo lỗi; icon + màn khởi động; giao diện tối | 0,75 |
| B6 | Chế độ soundcard: nhận USB, hỏi chế độ, đo qua loopback, cảnh báo nhạc lọt vào micro; test bằng tín hiệu giả (soundcard thật: khi có máy) | 0,5 |
| B7 | Chạy nền ngắn + tiếp tục việc dở; nhật ký thử nghiệm; IPA → bạn + 1–2 người thử (gộp phần thử M7) → sửa | 0,5 |
| B8 | Thử 5 người → sửa lỗi → báo cáo M8 (số đo mục 6, phiếu, vấn đề còn lại) | 1 |

Tổng ≈ 6,25 tuần (kế hoạch gốc: 0,5 DSP + 5 mobile). Phần tăng thêm là chạy nền và nhận file từ app
khác, hai việc M6 / M7 hẹn sang M8.

**Cách bạn tự kiểm tra khi xong M8:**
- Xoá app, cài lại → làm hết phần hướng dẫn lần đầu mà không cần đọc tài liệu.
- Từ Zalo, gửi một file nhạc → "Mở bằng ChuẩnTone" → Phân tích → Hát → hạ tone, đổi Bolero, thu
  30 giây → xuất video → Lưu vào Ảnh.
- Tắt app, mở lại → bài vừa hát nằm ở "Hát tiếp", vào là đúng tone và thể loại cũ.
- Cài đặt: đổi chất lượng xuất, xem dung lượng, xoá một bài.
- Cắm soundcard (nếu có) → chế độ soundcard → đo qua loopback → thu, xem có cảnh báo lẫn nhạc
  không.

---

## Phụ lục: bảng thuật ngữ (áp dụng cho mọi màn)

| Dùng | Không dùng | Ghi chú |
|---|---|---|
| Tone | key, giọng (khi nói về tone bài) | "Sol thứ (Gm)": tên Việt trước, ký hiệu trong ngoặc |
| Chỉnh giọng (Tắt / Tự nhiên / Ca sĩ / Robot) | Auto-Tune, pitch correction | spec mục 0 điểm 3 |
| Nhạc | beat, instrumental | "âm lượng nhạc"; tên file của người dùng giữ nguyên |
| Thể loại | preset, genre | chip Bolero, Ballad… |
| Vang | reverb, FX mix | |
| Nghe lại giọng | monitor, monitoring | |
| Giữ giọng ca sĩ gốc | vocal, stem | |
| Tách giọng | separation | |
| Bản thu | take, recording | |
| Giọng sớm / muộn | nudge, offset | |
| Độ trễ | latency | chỉ hiện số ms ở Nâng cao; người thường thấy "mượt / hơi trễ" |
| Soundcard | sound card, OTG | "cáp OTG" chỉ dùng khi hướng dẫn cắm |
