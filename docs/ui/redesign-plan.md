# Làm lại giao diện theo thiết kế Stitch — đánh giá và kế hoạch

> Trạng thái: **đã duyệt cả 5 quyết định ở mục 5** (2026-11-06), đang làm. Thiết kế:
> <https://www.figma.com/design/RfLdiGRCzIPD27fEsilq6y/Untitled>, 48 màn, tạo từ
> `docs/ui/stitch-prompts.md`.

## 1. Đánh giá

**Nên giữ:** phong cách đẹp và thống nhất hơn hẳn app hiện tại.
- Thẻ lớn dải đỏ → vàng ở Trang chủ.
- Màn hát nền tím đêm kiểu sân khấu.
- Thẻ bo tròn lớn, nút viên thuốc, chip thể loại.
- Dấu tiếng Việt đúng ở các màn đã xem. Không có chữ "Auto-Tune".
- Có đủ màn theo 20 prompt.

**Không làm theo nguyên xi được:** Stitch tự thêm nhiều thứ app không có, trái spec hoặc sai sự thật.
Vì vậy Figma chỉ là **mẫu hình ảnh**. Chức năng và câu chữ theo app thật.

### 1.1 Bỏ: tính năng Stitch tự nghĩ ra

| Trong thiết kế | Vì sao bỏ |
|---|---|
| Điểm số: "Score: 840", "Combo: x18", "Đúng nốt 92%", "96% Tuyệt phẩm", "98.5 điểm", "Chuẩn Tone 99.4%" | Chấm điểm nằm ngoài MVP (CLAUDE.md mục 5). Trên video xuất, đây là con số giả. |
| Tab "Khám phá / Chọn bài / Bảng vàng / Hồ sơ", ảnh đại diện, chuông thông báo, "Gói Miễn Phí", "Nâng cấp" | Chưa có tài khoản, thanh toán, bảng xếp hạng |
| "Đề xuất hát ngay", tên ca sĩ / tác giả trong thông tin bài, lọc bài theo ca sĩ | App chỉ có file bạn thêm vào, không có kho bài |
| "Khuyên dùng nữ/nam trầm", "Gợi ý giọng Nam", "Tone Nam/Nữ", "quãng Baritone" | App không dò quãng giọng |
| Thanh đáy "Chỉnh âm / Hát lại / Vocal Cut / Hoàn tất" và hàng nút thứ hai trên Màn hát | Trùng chức năng, nút Thu không còn rõ |
| "Tìm hiểu thêm", "Đề xuất", tải lại file ở thẻ lỗi | Không có màn / chức năng tương ứng, hoặc thêm việc không cần |

### 1.2 Sửa: thông tin sai sự thật

- "24-bit HD", "32-Bit Float", "44.1 kHz FLAC", "Hi-Res", "Studio Master", "Chuẩn 4K 60FPS", "320kbps":
  app ghi WAV 48 kHz, xuất AAC 128–256 kbit/s.
- "Micro: RODE Wireless GO": tên hãng có thật. App không hiện tên micro kiểu này.
- "Độ trễ 12 ms", "Micro: 12ms": số giả.
- "Vui lòng không ngắt tai nghe" ở bước đo: **ngược với app**, vì bước đó cần rút tai nghe để đo qua loa.
- "Hệ thống tự động cân chỉnh sân khấu…", "ChuẩnTone Vocal Engine đã kích hoạt", "AI Processing",
  "AI Sound Isolation 2.0", "Studio AI": chữ quảng cáo, sai hoặc khó hiểu với người dùng.

### 1.3 Pháp lý (CLAUDE.md mục 8)

- Tên bài và ca sĩ có thật lọt vào dữ liệu mẫu: Duyên Phận, Sầu Tím Thiệp Hồng, Đoạn Tuyệt, Nàng Thơ,
  Cơn Mưa Tháng Năm, Vài Lần Đón Đưa, Quang Lê, Lệ Quyên, Hà Anh Tuấn, Hoàng Dũng, Trịnh Công Sơn,
  Anh Bằng, Trúc Phương… Không dùng các màn này làm ảnh giới thiệu app.
- Có logo lạ "RKLES" ở màn đang tách giọng (`4012:4403`), không rõ nguồn: bỏ.
- Ảnh người hát do AI tạo: không đưa vào app.

### 1.4 Lỗi trình bày trong Figma (không cần sửa file, chỉ tránh khi code)

- Chữ bị cắt: "Phố Đêm Lấp Lái…", "Micro trong n…", "Soundcard / micro US…".
- Màn Cài đặt rộng 437 thay vì 390 ("Nâng cấp" tràn). Màn Nghe lại đầu tiên tràn phải. Hộp thoại
  soundcard lệch phải.
- Tiêu đề hỏng: "Ca Nhan", "Hình Hát Thông Báo Tách", "Điều Chỉnh Tone Nhạc".
- Ba kiểu thanh tab khác nhau. Nhiều màn lỗi / cảnh báo dùng bố cục Màn hát khác hẳn.
- Bảng dưới bị thanh đáy che (mất "Lọc tiếng ồn micro" ở bảng Âm lượng).
- Khung "video 9:16" không đúng tỉ lệ và lẫn nút bấm. "Màn SE" không đúng cỡ 375×667.
- Tên nốt lẫn lộn: "Sol#" / "Son#", "G#m" / "La thứ". App dùng "Son".
- Chữ phụ 12 px quá nhỏ cho người lớn tuổi; nên từ 14 px trở lên.
- Chữ trắng trên đầu vàng của dải màu tương phản thấp (`#FFB548` với trắng khoảng 1,7:1).

## 2. Hệ thống thiết kế lấy từ Figma

Đọc từ màn `4012:8104`. File không có Figma variables nên lấy trực tiếp từ các lớp.

| Token | Giá trị |
|---|---|
| Font | Be Vietnam Pro: Regular, SemiBold, Bold |
| Chính | `#B1212D`; đỏ phụ `#D43C42`; vàng `#FFB548` |
| Dải màu thẻ lớn | `#B1212D → #D43C42 → #FFB548` |
| Nền / mặt phẳng | `#FFF7FF`, `#FFFFFF`, `#F4EBF7`, `#EEE5F1`; tint đỏ `#FFDAD8` |
| Chữ | `#1E1A22`; chữ phụ `#5A403F` |
| Đúng nốt | nền `#7EFBAB`, chữ `#006A3A` |
| Bo góc | thẻ 32, nút viên thuốc; nút chính cao 56–64 |
| Khoảng cách | lề 16, giữa các khối 24 |

Thêm cho app:
- Nút Thu luôn đỏ đặc, không dải màu.
- Nút chính chữ trắng chỉ dùng dải đỏ `#B1212D → #D43C42`; vàng để điểm nhấn.
- Chữ phụ tối thiểu 14.

## 3. Màn hình → Figma

| Màn trong app | Frame mẫu |
|---|---|
| Trang chủ | `4012:8104` (4 tab), chưa có bài `4012:237` |
| Hướng dẫn 1–6 | `4012:505`, `4012:424` + `4012:615`, `4012:673` + `4012:752`, `4012:840`, `4012:1134` + `4012:1029`, `4012:950` |
| Thêm bài | `4012:1352` |
| Phân tích bài | `4012:1200`, `4012:1515` |
| Màn hát | `4012:1671`, đếm ngược `4012:1916`, camera `4012:2181`, tối `4012:7124`, chữ to `4012:7368` |
| Mẹo lần đầu | `4012:2447`, `4012:2716`, `4012:2985` |
| Các bảng | Lời `4012:3262`, Âm lượng `4012:3461`, Sửa tone `4012:3646` |
| Tách giọng | `4012:3867`, `4012:3941`, `4012:4403`, `4012:4058`, `4012:4196`, `4012:6904` |
| Nghe lại & xuất | `4012:5240`, `4012:5499`, `4012:5707` |
| Danh sách | `4012:5839`, `4012:6080` |
| Cài đặt | `4012:4524`, `4012:4905`, `4012:5087` |
| Hộp thoại, lỗi | `4012:6653`, `4012:7007` |

## 4. Các bước

Chỉ đổi lớp giao diện. Không đụng engine, audio callback, `SingController`, xuất, tách giọng.
Không thêm thư viện; có thể thêm font (quyết định 2 ở mục 5). Icon dùng Material Icons Rounded có sẵn.
Hình minh hoạ vẽ bằng icon + dải màu trong code, không dùng ảnh AI.

| Bước | Nội dung | Ước lượng |
|---|---|---|
| U0 | Theme: màu sáng / tối, font, chữ, bo góc; dải màu; test tương phản | 0,5 ngày |
| U1 | Thành phần chung: nút chính, thẻ, chip, thanh trượt, thanh đáy, bảng dưới, trạng thái rỗng | 0,5 |
| U2 | Trang chủ + khung tab; Thêm bài; Bài hát của tôi; Bản thu của tôi | 1 |
| U3 | Hướng dẫn 6 bước; Phân tích bài | 0,5 |
| U4 | Màn hát (đếm ngược, thu, camera, mẹo lần đầu) + các bảng + tách giọng | 1,5 |
| U5 | Nghe lại & xuất; bảng "Video đã sẵn sàng" | 0,5 |
| U6 | Cài đặt, Micro, Đo độ trễ, hộp thoại, báo lỗi | 0,5 |
| U7 | Giao diện tối, iPhone SE chữ 130%; chạy lượt tự động, chụp ảnh đối chiếu Figma; cập nhật test, tài liệu | 0,5 |

Tổng khoảng 5 ngày làm việc. Mỗi bước: test xanh, push, CI, kèm ảnh chụp để bạn xem.

Giữ nguyên chữ của các nút chính để test và kịch bản thử vẫn đúng. Bảng thuật ngữ M8 vẫn áp dụng.

Chưa làm: mẫu video xuất 9:16 (vẽ bằng Swift / AVFoundation), để sau B8.

## 5. Cần bạn quyết định

1. **Điều hướng:** 4 tab "Trang chủ / Bài hát / Bản thu / Cài đặt" (đề xuất; quen thuộc như Zalo,
   TikTok), hay giữ một Trang chủ như hiện nay.
2. **Font Be Vietnam Pro:** license SIL OFL 1.1, cho dùng thương mại và đóng gói vào app, nhưng
   không nằm trong danh sách MIT/BSD/Apache/Zlib/ISC ở CLAUDE.md nên cần bạn đồng ý. Ba cỡ chữ
   thêm khoảng dưới 1 MB (ước lượng). Nếu không đồng ý thì dùng font hệ thống (SF), chữ sẽ khác
   thiết kế.
3. **Điểm số:** bỏ hẳn khỏi bản này (đề xuất, đúng spec). Nếu muốn có chấm điểm thì là tính năng
   mới, cần kế hoạch riêng.
4. **Màn hát luôn nền tối** kể cả khi máy ở giao diện sáng (đề xuất, như thiết kế).
5. **Thứ tự:** làm giao diện mới xong rồi mới thử trên iPhone và B8 (đề xuất, đỡ thử hai lần).

## 6. Tiến độ

- [x] **U0, U1, U2** (2026-11-07):
  - Theme: màu từ Figma, font Be Vietnam Pro (3 cỡ, ~410 KB, giấy phép OFL ở Cài đặt → Giấy phép
    thư viện), chữ phụ tối thiểu 14. `test/theme_test.dart` kiểm tương phản ≥ 4,5:1.
  - `widgets/app_ui.dart`: `HeroCard`, `SectionHeader`, `IconBadge`, `Pill`, `AppChoiceChip`,
    `EmptyState`, `StageBackground`.
  - Khung 4 tab `MainShell`; Trang chủ mới; bảng "Thêm bài hát" (chọn từ Tệp, ba bước gửi từ
    Zalo); Bài hát của tôi; Bản thu của tôi (sửa câu cũ còn nhắc Phòng thử).
- [x] **U3** (2026-11-07): Hướng dẫn 6 bước (ô minh hoạ, thẻ trạng thái, dấu chấm bước); Phân tích bài
  (các bước dạng thẻ, thẻ kết quả đỏ, nút "Hát" cố định ở đáy).
- [x] **U4–U7** (2026-11-07):
  - Màn hát luôn nền tối kiểu sân khấu (cả bảng dưới, hộp thoại mở từ màn này); thẻ mờ cho lời và
    các nút chỉnh; camera lên đầu màn; đếm ngược số to giữa màn; thanh đáy bo góc; nút Thu đỏ đặc.
  - Sửa tone thành bảng dưới với lưới 12 nốt (Màn hát, Phân tích bài).
  - Nghe lại: các khối dạng thẻ; bảng "Video đã sẵn sàng" sau khi xuất video (Lưu vào Ảnh, Chia sẻ,
    Xong). Chia sẻ dùng bảng chia sẻ của máy, không có nút riêng từng app (quyết định M7).
  - Cài đặt nhóm thành thẻ; Micro, Đo độ trễ theo màu mới.
  - Lượt chạy tự động đi qua cả bảng Thêm bài, Âm lượng, Sửa tone, Lời, Micro, Đo độ trễ; hai cỡ máy.
  - Còn lại sau U4–U7: đồng hồ kim ở Đo độ trễ, sóng âm ở Nghe lại, mẫu video 9:16 → làm ở U8.
- [x] **U8** (2026-11-12):
  - Nghe lại: hình sóng bản thu (96 cột, độ to giọng khô + nhạc, tính ở isolate riêng khi mở màn)
    thay thanh tua; chạm / kéo để tua, phần đã nghe màu chính, phần bị cắt đầu / đuôi mờ đi. Chưa
    tính xong thì vẫn hiện thanh tua cũ.
  - Đo độ trễ: đồng hồ kim nửa vòng 0–100 ms, ba vùng xanh / cam / đỏ cùng ngưỡng với câu nhận xét
    (35 / 60 ms).
  - Mẫu video sóng nhạc 9:16 theo khung Figma: nền tối ngả màu thể loại, quầng sáng, đĩa nhạc,
    32 cột đối xứng (dải trầm ở giữa), tên bài to (dài thì 2 dòng chữ nhỏ hơn, thừa thì "…"), dòng
    phụ vàng, nhãn "Bản thu bởi ChuẩnTone", thanh thời gian mảnh, thương hiệu "ChuẩnTone · Bấm bài
    là hát". Bỏ các chi tiết Figma tự nghĩ ra: "32-Bit Float", "Hi-Res Audio / Studio Master",
    "Chuẩn Tone 99.4%" (điểm số), câu lời trích. Phần tĩnh vẽ một lần; mỗi khung ~1,8 ms trên Mac
    (xem trước bằng mã vẽ iOS chạy trên macOS) — cần xem video thật trên iPhone.
  - Bảng Tách giọng: nút chính màu chính (trước là tông vàng phụ, đục trên nền tối), nhãn "Giữ
    giọng ca sĩ gốc" một dòng phía trên thanh trượt (vấn đề #13). Lượt chạy tự động với máy chủ
    thật trên giao diện mới: bài 4:32 tách xong sau 58 s (máy chủ 54 s).

