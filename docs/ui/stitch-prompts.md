# Thiết kế lại giao diện ChuẩnTone bằng Stitch → Figma

> Tài liệu làm việc (2026-11-06). Prompt viết bằng tiếng Anh, vì Stitch hiểu tiếng Anh tốt hơn.
> Chữ trên giao diện để nguyên tiếng Việt trong ngoặc kép, Stitch sẽ chép đúng. Tên bài và lời
> trong prompt là tên tự đặt, không dùng bài có bản quyền.

## 1. Cách dùng Stitch

1. Vào stitch.withgoogle.com, đăng nhập Google, chọn **App** (mobile, không phải Web).
2. Chọn chế độ có nút **Copy to Figma** để xuất sang Figma (lúc ra mắt là chế độ Standard). Chế
   độ Experimental cho tải ảnh lên làm mẫu nhưng có thể không xuất Figma được.
3. Dán **Prompt 1** trước. Prompt này đặt bối cảnh và hệ thống thiết kế, rồi tạo Trang chủ. Các
   prompt sau tạo màn mới **trong cùng project** để giữ chung phong cách.
4. Sau mỗi màn, nếu cần sửa thì chọn màn đó và gõ yêu cầu ngắn, mỗi lần 1–2 thay đổi, vd "Make
   the record button bigger", "Use more spacing between chips".
5. Mở bảng **Theme** của Stitch, đặt:
   - màu chính `#E5484D`;
   - font **Be Vietnam Pro** (không có thì dùng Inter — cả hai đủ dấu tiếng Việt);
   - bo góc lớn.
6. Xuất: chọn các màn → **Copy to Figma** → dán vào file Figma. Kiểm lại dấu tiếng Việt, đôi khi
   Stitch làm hỏng dấu.
7. Có thể tải ảnh màn hiện tại (`testdata/walkthrough/iphone13/*.png`) lên chế độ Experimental,
   kèm câu "Redesign this screen to be more attractive, keep all functions".

Thiết kế xong, gửi link Figma: Claude đọc qua Figma MCP và làm theo trong Flutter. Theo CLAUDE.md,
phải lập kế hoạch và được duyệt trước khi code. Nên làm xong giao diện mới trước buổi thử 5 người
(B8), để buổi thử đánh giá đúng giao diện sẽ phát hành.

## 2. Tính năng đầy đủ theo màn hình

"(đề xuất)" = chưa có trong app, thêm trong bản thiết kế mới.

**Hướng dẫn lần đầu (6 bước)**
1. Chào: "Bấm bài là hát", 3 điểm chính.
2. Micro: giải thích rồi mới xin quyền. Bị từ chối thì "Mở Cài đặt" / "Để sau".
3. Đo nhanh 5 giây qua loa, kết quả nói bằng lời.
4. Tai nghe: nhận ra có dây / Bluetooth / loa ngoài.
5. Thử giọng: thanh mức micro, nghe lại giọng có vang; "Rõ rồi" / "Chưa" + mẹo.
6. Xong: "Thêm bài hát đầu tiên" / "Hát thử bài mẫu" / "Về Trang chủ".

Có nút "Bỏ qua"; xem lại được trong Cài đặt.

**Trang chủ**
- Tên app + khẩu hiệu, nút Cài đặt.
- Nút lớn "Thêm bài hát" (Từ Tệp, Zalo, Safari…).
- "Hát tiếp": 3 bài gần nhất, mỗi bài có tone, mức tone đã đổi, có lời hay không.
- "Tất cả bài hát (n)".
- Bài mẫu luyện giọng: Đô Rê Mi, La thứ.
- "Bản thu của tôi (n)".
- Thẻ "Đang tách giọng dở" → bấm để tách tiếp.
- Trạng thái chưa có bài.

**Thêm bài**
- Chọn file từ Tệp (MP3, M4A, WAV, kèm file lời `.lrc`).
- "Mở bằng ChuẩnTone" từ Zalo / Tệp. File lời gửi riêng tự gắn vào bài cùng tên.
- (đề xuất) Bảng chọn cách thêm, có hướng dẫn gửi từ Zalo.

**Phân tích bài**
- Các bước có tiến độ: Tách giọng ca sĩ (không bắt buộc), Dò tone, Dò nhịp.
- Kết quả "Tone: La thứ (Am) – 92 BPM".
- Sửa tone tay.
- Nút "Hát".

**Màn hát (màn chính)**
- Tên bài; nút loa (âm lượng, nghe lại giọng); ⋯ (Lời bài hát, Tách giọng ca sĩ, Sửa tone).
- Tone hiện tại thật to + nguồn ("theo bài" / "tone bạn đã sửa" / "chọn tay").
- Tone − / + (±6, nhớ theo bài, giữ nhịp).
- Đồng hồ cao độ nói bằng lời: "Hơi cao" / "Cao quá" / "Đúng nốt".
- Lời chạy chữ karaoke.
- Chỉnh giọng: Tắt / Tự nhiên / Ca sĩ / Robot.
- Thể loại: Khô, Bolero, Ballad, Nhạc trẻ, Remix, Rap, Lofi — tự gợi ý theo tên bài và nhịp.
- Thanh "Vang"; "Giữ giọng ca sĩ gốc" (khi đã tách).
- Thanh đáy: vị trí, lùi 10 giây, Phát / Dừng, nút **Thu** đỏ, bật camera.
- Khi thu: đếm ngược 3-2-1, "Dừng · 0:42", khoá tone. Hết bài thì tự dừng → Nghe lại.
- Cảnh báo Bluetooth / loa ngoài; mẹo lần đầu (3 bước); chế độ soundcard.

**Các bảng trên Màn hát**
- Âm lượng: nghe lại giọng, âm lượng giọng / nhạc, lọc tiếng ồn.
- Lời bài hát: chọn file, lời sớm / muộn, bỏ lời.
- Sửa tone: 12 nốt × Trưởng / Thứ.
- Tách giọng ca sĩ: hỏi đồng ý gửi bài, tiến độ, Huỷ, "Giữ giọng ca sĩ gốc", Xoá bản tách,
  tiếp tục khi bị ngắt.

**Nghe lại & chỉnh**
- Phát bản thu.
- Âm lượng giọng / nhạc.
- Giọng sớm / muộn ±.
- Cắt đầu / đuôi.
- Đổi chỉnh giọng và thể loại trên giọng đã thu (nghe ngay).
- Xuất video sóng nhạc 9:16 / video camera 9:16 / âm thanh M4A, có tiến độ và Huỷ.
- Kết quả: "Lưu vào Ảnh", "Chia sẻ" (TikTok, Facebook, Zalo qua bảng chia sẻ của máy).
- Cảnh báo nhạc lọt vào micro (soundcard). Xoá bản thu.
- (đề xuất) Bảng "Video đã sẵn sàng" có nút chia sẻ từng app.

**Mẫu video xuất 9:16**
- Nền sóng nhạc + tên bài + thể loại / ngày.
- Bản camera trước: hình người hát khớp tiếng.

**Bài hát của tôi**
- Tìm (gõ không dấu cũng được), đổi tên, xoá, thêm bài.

**Bản thu của tôi**
- Danh sách (ngày, độ dài, dung lượng), mở lại, xoá.

**Cài đặt**
- Âm thanh: đo độ trễ từng bước, chọn micro + thanh mức, tự bật nghe lại khi cắm tai nghe, chế
  độ soundcard.
- Xuất: 128 / 192 / 256 kbit/s, video 720p, cân bằng âm lượng.
- Bộ nhớ: từng nhóm + xoá.
- Trợ giúp: xem lại hướng dẫn, mẹo hát.
- Giới thiệu: phiên bản, quyền riêng tư, giấy phép.
- Nâng cao (cho người thử): nhật ký thử nghiệm, Phòng thử, đo độ trễ kỹ thuật, máy chủ tách
  giọng.

**Hộp thoại / trạng thái**
- "Bạn vừa cắm soundcard?"
- Lỗi bằng câu thường + nút "Chi tiết".
- Mất mạng khi tách giọng.
- Thông báo ngắn.

**Ngoài MVP (chưa thiết kế):** chấm điểm, song ca, livestream, kho bài, tài khoản.

## 3. Prompt cho Stitch (dán lần lượt)

### Prompt 1 — Bối cảnh, hệ thống thiết kế, Trang chủ

```
Design a mobile app for iPhone (390×844) called "ChuẩnTone", a Vietnamese karaoke app. Tagline: "Bấm bài là hát".
Users: Vietnamese amateur singers aged 18–50 (bolero lovers, young pop fans, TikTok livestreamers), many not tech-savvy.
Promise: pick a song → the app detects its key, corrects your pitch in real time, adds the right vocal effects for the genre, records, and exports a 9:16 video to share on TikTok, Facebook, Zalo.

Visual style: warm, joyful "evening karaoke stage". Soft spotlight gradients (rose-red #E5484D to warm amber #FFB547) only on hero areas; everything else clean and calm so controls are easy to read. Big touch targets (primary buttons 56–64 pt tall, pill-shaped), large readable text (body at least 16 pt), high contrast, generous spacing, rounded cards (20–24 pt radius), Material Symbols Rounded icons, subtle shadows.
Colors: primary #E5484D (the Record button is always this red), secondary #FFB547, background #FFF8F5, surface #FFFFFF, text #1E1A22, success green #2FB36B (on pitch), warning orange #FF9500 (slightly off pitch).
Font: "Be Vietnam Pro", bold headings.
All UI text must be in Vietnamese exactly as written in quotes. Never use the word "Auto-Tune" anywhere.

Screen: Home ("Trang chủ"), top to bottom:
- Header: "ChuẩnTone" in large bold, "Bấm bài là hát" below, a settings gear icon at top right.
- Hero card (full width, spotlight gradient): the biggest button on the screen, "Thêm bài hát", with the subtitle "Từ Tệp, Zalo, Safari…" and a music-plus icon.
- Section "Hát tiếp": 3 song rows. Each row has a gradient artwork square with a music note, the title, a meta line, and a round play button on the right:
  "Chuyến Đò Chiều" — "La thứ (Am) · tone −1 · có lời"
  "Phố Đêm Lấp Lánh" — "Rê trưởng (D) · có lời"
  "Hoa Nắng Bên Thềm" — "Son trưởng (G)"
- A row link "Tất cả bài hát (12)" with a chevron.
- Section "Bài mẫu": 2 cards side by side:
  "Luyện giọng: Đô Rê Mi" — "Đô trưởng · hát theo nốt dẫn · 1 phút"
  "Luyện giọng: La thứ" — "La thứ · chạy nốt và ngân dài · 1 phút"
- A wide tonal button "Bản thu của tôi (4)" with a waveform icon.
```

### Prompt 2 — Trang chủ khi chưa có bài, thẻ tách giọng dở

```
Using the same ChuẩnTone design system, create a variant of the Home screen for a first-time user with no songs yet.
Keep the header and the "Thêm bài hát" hero card.
Replace "Hát tiếp" with a friendly empty-state card: a small illustration of a phone receiving a music file, the title "Chưa có bài nào", and the text "Mở file nhạc trong Zalo hoặc Tệp rồi chọn "Mở bằng ChuẩnTone" — hoặc hát thử bài mẫu bên dưới."
Keep the "Bài mẫu" section and the "Bản thu của tôi" button (no count).
Also design a small amber-tinted banner card that can appear under the hero card on the normal Home screen: "Đang tách giọng dở: Chuyến Đò Chiều", with the button "Tiếp tục".
```

### Prompt 3 — Hướng dẫn bước 1 (Chào) và bước 2 (Micro)

```
Using the same design system, create 2 onboarding screens with a shared layout:
- Top bar: "Bước 1/6" centered, a text button "Bỏ qua" on the right.
- A large rounded illustration area.
- A bold title and body text.
- At the bottom: a full-width primary pill button, a secondary text button under it, and small step dots.

Screen A — "Bước 1/6":
- Illustration: a smiling person singing into a phone, with music notes and sparkles.
- Title: "Bấm bài là hát".
- Three rows with round icons: "Tự dò tone của bài hát", "Tự chỉnh giọng cho đúng nốt", "Tự chọn hiệu ứng theo thể loại".
- Small text: "Mất khoảng 1 phút: cho phép micro, đo máy, thử giọng."
- Primary button: "Bắt đầu".

Screen B — "Bước 2/6":
- Illustration: a big friendly microphone.
- Title: "Micro".
- Body: "ChuẩnTone cần micro để nghe giọng bạn hát, chỉnh giọng cho đúng nốt và thu âm. Âm thanh không rời khỏi máy (trừ khi bạn tự bấm tách giọng ca sĩ và đồng ý)."
- Primary button: "Cho phép dùng micro".
- Also design the permission-denied state of Screen B: a soft red notice "Micro đang bị chặn. Vào Cài đặt của máy → ChuẩnTone → bật Micro.", primary button "Mở Cài đặt", secondary "Để sau".
```

### Prompt 4 — Hướng dẫn bước 3 (Đo nhanh) và bước 4 (Tai nghe)

```
Using the same onboarding layout, create:

Screen A — "Bước 3/6", with 3 states:
- Ready:
  - Illustration: a speedometer with sound waves.
  - Title: "Đo nhanh (5 giây)".
  - Body: "Để máy chỗ yên tĩnh, tăng âm lượng khoảng 70%. Máy sẽ kêu xè xè một lúc — số đo giúp giọng thu khớp nhạc."
  - Primary button "Đo", secondary "Bỏ qua bước này".
- Measuring: animated sound waves, the text "Đang đo…", the button disabled.
- Result:
  - A big green check and the title "Máy bạn hát mượt".
  - Body: "Nghe lại giọng gần như tức thì. Dùng tai nghe có dây để không bị hú và nghe rõ nhất."
  - Primary button "Tiếp", secondary "Đo lại".

Screen B — "Bước 4/6":
- Illustration: wired headphones.
- Title: "Tai nghe".
- Body: "Cắm tai nghe có dây (cổng 3,5 mm hoặc adapter Lightning / USB-C) để nghe lại giọng mình khi hát."
- A status card showing the active state in green: "Đã cắm tai nghe có dây: bạn sẽ nghe lại giọng mình khi hát."
- Also show the 2 other possible states as separate small cards:
  - amber: "Tai nghe Bluetooth: chỉ nghe được nhạc, không nghe lại được giọng vì bị trễ."
  - grey: "Đang dùng loa ngoài: vẫn hát được nhưng không nghe lại giọng."
- Primary button: "Tiếp".
```

### Prompt 5 — Hướng dẫn bước 5 (Thử giọng) và bước 6 (Xong)

```
Using the same onboarding layout, create:

Screen A — "Bước 5/6":
- Title: "Thử giọng".
- Body: "Nói "a lô" hoặc hát thử: bạn sẽ nghe giọng mình có vang trong tai nghe."
- A large live microphone level meter in the center: a pulsing circular ring around a mic icon, green when the voice is heard.
- Question: "Nghe rõ giọng mình chưa?"
- Two buttons side by side: outlined "Chưa", filled "Rõ rồi".
- Also a state after "Chưa": a list of tips ("Cắm tai nghe có dây — tai nghe Bluetooth không nghe lại được giọng.", "Tăng âm lượng máy.", "Nói gần micro hơn (cách miệng một gang tay).") with primary "Tiếp".

Screen B — "Bước 6/6":
- Illustration: a celebration with confetti and a microphone.
- Title: "Xong! Hát thôi".
- Body: "Thêm bài từ app Tệp, hoặc mở file nhạc trong Zalo / Safari rồi chọn ChuẩnTone. Chưa có bài thì hát thử bài mẫu luyện giọng."
- Buttons: primary "Thêm bài hát đầu tiên", tonal "Hát thử bài mẫu", text button "Về Trang chủ".
```

### Prompt 6 — Thêm bài hát (đề xuất)

```
Using the same design system, create a bottom sheet over the Home screen titled "Thêm bài hát".
Two big option tiles:
1) Folder icon, "Chọn từ Tệp", with the subtitle "MP3, M4A, WAV — chọn kèm file lời .lrc nếu có".
2) Chat-bubble icon, "Gửi từ Zalo", with 3 numbered mini steps: "Mở tin nhắn có file nhạc", "Bấm giữ file → Mở bằng ứng dụng khác", "Chọn ChuẩnTone".
Footer note in small grey text: "ChuẩnTone không tải nhạc từ mạng — bạn dùng bài mình có."
Add a drag handle and a close button.
```

### Prompt 7 — Phân tích bài

```
Using the same design system, create the screen "Phân tích bài" (back arrow in the app bar).
- The song title "Chuyến Đò Chiều", large.
- A vertical checklist of 3 steps with status icons and thin progress bars:
  1) "Tách giọng ca sĩ" — optional, grey dash icon, subtitle "Không bắt buộc — dùng cho bài gốc có giọng ca sĩ.", small tonal button "Tách".
  2) "Dò tone" — done, green check.
  3) "Dò nhịp" — running, spinner and a progress bar at 70%.
- A result card with the spotlight gradient: "Tone: La thứ (Am)" very large, "92 BPM" below, and a small outlined button with a pencil icon, "Sửa tone".
- Bottom: a full-width primary button "Hát" with a microphone icon, 64 pt tall.
Also show the in-progress state: the result card is a shimmering placeholder and the "Hát" button is disabled.
```

### Prompt 8 — Màn hát (màn quan trọng nhất)

```
Using the same design system, create the Singing screen, the heart of the app. Give it a "stage" feel: a deep plum gradient background (#2A1630 to #15111A) with a soft warm spotlight from the top, white text, rose-red accents. Top to bottom:
- App bar: back arrow, song title "Phố Đêm Lấp Lánh", a speaker icon (volume and voice monitoring), and a "⋯" more icon.
- Key hero:
  - The current key in huge bold text, "La thứ (Am)", with a small caption "theo bài".
  - Below it, a tone stepper: a round "−" button, the label "Tone −1" in bold, a round "+" button.
  - Caption: "Đã hạ 1 nửa cung, giữ nguyên nhịp."
- Pitch meter card:
  - On the left, the big note name "Mi4" in orange, with the label "Hơi cao" in orange.
  - A horizontal track from "Thấp" to "Cao" with a green center zone and a glowing dot.
  - When on pitch, it turns green and shows "Đúng nốt".
- Karaoke lyrics:
  - Current line large, the sung part highlighted in rose-red: "Gió về ngang sông chiều nay".
  - Next line smaller and dimmer: "Thuyền ai neo bến đợi chờ".
- "Chỉnh giọng": 4 pill chips — "Tắt", "Tự nhiên" (selected), "Ca sĩ", "Robot".
- "Thể loại": horizontally scrollable chips — "Khô", "Bolero" (selected), "Ballad", "Nhạc trẻ", "Remix", "Rap", "Lofi". Caption: "Gợi ý theo nhịp 92 BPM".
- A slider "Vang" with the value "120%".
- Elevated bottom bar with a rounded top:
  - A seek bar with the times "1:05" and "4:12".
  - A row: a "rewind 10 s" icon button, a big round play/pause button, a large red pill record button "● Thu" (the most prominent control), and a camera toggle icon "Quay video".
```

### Prompt 9 — Màn hát khi thu (đếm ngược, đang thu có camera)

```
Using the Singing screen design, create 2 states:
A) Countdown before recording:
   - A dim overlay with a huge "3" in the center, inside a pulsing ring.
   - The text "Chuẩn bị hát…".
   - The record button shows "Huỷ".
B) Recording with the front camera on:
   - A rounded 9:16 camera preview card (placeholder of a person singing) at the top, replacing the key hero.
   - The key shown small in a pill, "La thứ (Am) · Tone −1".
   - A red recording indicator "● Đang thu 0:42" at the top.
   - The pitch meter and the lyrics below the preview.
   - The record button becomes "■ Dừng · 0:42" with a pulsing red ring.
   - The tone buttons are disabled, with the hint "Đang thu: tone được giữ nguyên."
```

### Prompt 10 — Mẹo lần đầu trên Màn hát

```
On the Singing screen, design a first-time coach mark:
- Dim the screen and spotlight the tone stepper.
- Show a rounded tooltip card with:
  - title "Tone";
  - text "Tone của bài hiện ở đây. Hát thấy cao quá hay thấp quá thì bấm − / + để hạ hoặc nâng tone nhạc. App nhớ tone cho từng bài.";
  - buttons "Bỏ qua" and "Tiếp (1/3)".
Make 2 more in the same style:
- spotlight on "Chỉnh giọng": "Chỉnh giọng giúp bạn hát đúng nốt. Tự nhiên: nhẹ nhàng; Ca sĩ: chuẩn hơn; Robot: hiệu ứng máy." — "Tiếp (2/3)";
- spotlight on the "Thu" button: "Bấm Thu để thu bài hát. Thu xong bạn nghe lại, chỉnh, rồi xuất video để chia sẻ." — "Đã hiểu".
```

### Prompt 11 — Các bảng trên Màn hát (âm lượng, lời, sửa tone)

```
Using the same design system, create 3 bottom sheets for the Singing screen:
1) "Âm lượng":
   - Switch "Nghe lại giọng", subtitle "Nghe giọng mình qua tai nghe khi hát".
   - Sliders "Âm lượng giọng" 100% and "Âm lượng nhạc" 80%.
   - Switch "Lọc tiếng ồn micro", subtitle "Giảm tiếng ồn nền lúc không hát".
   - An amber warning row: "Tai nghe Bluetooth bị trễ nhiều nên đã tắt nghe lại giọng. Hãy dùng tai nghe có dây."
2) "Lời bài hát":
   - Status "Đã có lời: 32 dòng."
   - A stepper "Lời sớm hơn / Lời muộn hơn" with the value "Lời đúng như file".
   - Buttons "Đổi file lời" and text button "Bỏ lời".
3) "Sửa tone của bài":
   - A 4×3 grid of note names "Đô", "Đô#", "Rê", "Rê#", "Mi", "Fa", "Fa#", "Son", "Son#", "La", "La#", "Si", with "La" selected.
   - A segmented choice "Trưởng" / "Thứ", with "Thứ" selected.
   - A preview "La thứ (Am)".
   - Buttons "Về tone dò được" (text) and "Lưu" (filled).
```

### Prompt 12 — Tách giọng ca sĩ

```
Using the same design system, create the vocal separation flow.
1) Consent dialog:
   - Title "Gửi bài để tách giọng?"
   - Text "Bài hát sẽ được gửi lên máy chủ ChuẩnTone chỉ để tách giọng ca sĩ khỏi nhạc. Máy chủ không nhận tên bài và tự xoá bài ngay khi tách xong (chậm nhất sau 24 giờ)."
   - Checkbox "Không hỏi lại".
   - Buttons "Không" and "Đồng ý".
2) Bottom sheet "Tách giọng ca sĩ" with 3 states:
   - Idle: the text "Dùng cho bài gốc có giọng ca sĩ. Nhạc karaoke (không lời) thì không cần." and the primary button "Tách giọng ca sĩ".
   - In progress: "Đang tách giọng… 3/14" with a progress bar, a small text "Bạn vẫn hát được trong lúc chờ", and a text button "Huỷ".
   - Done: "Tách xong sau 60 giây", a slider "Giữ giọng ca sĩ gốc" at 30% with the caption "Nghe giọng ca sĩ nhỏ để hát theo", and a text button "Xoá bản tách".
3) A small card on the Singing screen for an interrupted job: "Tách giọng ca sĩ chưa xong." with the buttons "Tiếp tục tách" and "Bỏ".
```

### Prompt 13 — Nghe lại & chỉnh

```
Using the same design system, create the screen "Nghe lại & chỉnh" (back arrow, trash icon).
- Song title "Phố Đêm Lấp Lánh", meta "05/11 21:30 · 0:30 · Tai nghe có dây".
- A waveform scrubber (the played part in rose-red), the times "0:12" and "0:30", and a big round play button.
- Section "Giọng và nhạc": sliders "Âm lượng giọng" 100% and "Âm lượng nhạc" 60%.
- Section "Giọng sớm / muộn": a − / + stepper with the value "Như lúc thu" and the hint "Nghe giọng chậm hơn nhạc thì bấm +, nhanh hơn thì bấm −."
- Section "Cắt đầu / đuôi": a range selector drawn over a mini waveform, with "Dài 0:30".
- Section "Chỉnh giọng": the same chips as the Singing screen.
- Section "Thể loại": chips, the description "Vang dài, ấm, có tiếng vọng nhẹ", and the slider "Vang".
- A sticky bottom bar, always visible: a full-width primary button "Xuất video sóng nhạc (9:16)" with a film icon, and above it a small secondary link "Xuất âm thanh (M4A)".
```

### Prompt 14 — Xuất xong, mẫu video 9:16

```
Using the same design system, create:
A) Exporting state of the sticky bar: "Đang dựng video… 45%" with a progress bar and "Huỷ".
B) Bottom sheet "Video đã sẵn sàng":
   - A big check and a 9:16 thumbnail of the video.
   - The info "0:30 · 6,2 MB".
   - Primary button "Lưu vào Ảnh".
   - A row of round share buttons labeled "TikTok", "Facebook", "Zalo", "Khác".
C) The exported video frame itself (9:16, 1080×1920):
   - A dark gradient background in the genre color (purple for Bolero).
   - A glowing audio waveform visualizer in the center.
   - The song title "Phố Đêm Lấp Lánh" large, with the subtitle "Bolero · 05/11/2026".
   - A small "ChuẩnTone" watermark at the bottom.
   - Also a camera version: the singer's front-camera video fills the frame, the title is in a lower third, same watermark.
```

### Prompt 15 — Bài hát của tôi, Bản thu của tôi

```
Using the same design system, create 2 list screens:
A) "Bài hát của tôi":
   - A search field "Tìm bài (gõ không dấu cũng được)".
   - Rows with: a gradient artwork, the title, the meta "La thứ (Am) · 92 BPM · có lời", and a "⋯" menu with "Đổi tên" and "Xoá bài".
   - A floating button "Thêm bài".
   - Empty search state: "Không có bài nào khớp."
B) "Bản thu của tôi":
   - Rows grouped by "Hôm nay" and "Tuần trước".
   - Each row: a waveform icon (or a video thumbnail if recorded with the camera), the title, "21:30 · 0:30 · 17,5 MB", and a badge "Đã xuất video" when exported.
   - A trash icon that opens the dialog "Xoá bản thu?" with "Xoá cả các file đã xuất của bản thu này." and the buttons "Huỷ" / "Xoá".
   - Empty state: "Chưa có bản thu nào — vào một bài và bấm Thu."
```

### Prompt 16 — Cài đặt

```
Using the same design system, create the "Cài đặt" screen with grouped rounded sections:
- "Âm thanh":
  - "Đo độ trễ", subtitle "Máy bạn hát mượt · 24 ms".
  - "Micro", subtitle "Máy tự chọn".
  - Switch "Tự bật nghe lại giọng khi cắm tai nghe", subtitle "Tắt khi về loa ngoài để không bị hú".
  - Switch "Chế độ soundcard".
- "Xuất":
  - "Chất lượng âm thanh" with a segmented control "128" / "192" / "256" kbit/s.
  - Switch "Video 720p".
  - Switch "Cân bằng âm lượng cho mạng xã hội".
- "Bộ nhớ": rows "Bài hát · 1,2 GB", "Bản thu · 850 MB", "Giọng đã tách · 300 MB", each with a "Xoá" text button.
- "Trợ giúp":
  - "Xem lại hướng dẫn", subtitle "Micro, đo máy, tai nghe, thử giọng".
  - "Mẹo hát", subtitle "Tai nghe, chỉnh tone, khi nào dùng Robot…".
- "Giới thiệu": "ChuẩnTone 1.0.0", "Quyền riêng tư" (subtitle "Khi nào âm thanh được gửi đi"), "Giấy phép thư viện".
- "Nâng cao": smaller, grey, collapsed by default — "Ghi nhật ký thử nghiệm", "Phòng thử", "Máy chủ tách giọng".
```

### Prompt 17 — Đo độ trễ, Micro

```
Using the same design system, create 2 screens:
A) "Đo độ trễ" wizard with a 3-step indicator:
   - Step 1: "Rút tai nghe, để máy chỗ yên tĩnh", button "Đo".
   - Step 2: a measuring animation, "Đang đo…".
   - Step 3: a result card with a friendly semicircle gauge — green zone "Máy bạn hát mượt", amber "Hơi vọng", red "Nên tắt nghe lại giọng" — with the needle in the green zone, the text "Nghe lại giọng gần như tức thì.", and the buttons "Đo lại" and "Xong".
B) "Micro":
   - A big live input level meter with a red clip zone and the warning "Hát to quá, micro bị vỡ tiếng — lùi xa micro một chút".
   - A radio list to choose the microphone:
     "Tự động" — "Máy tự chọn: có tai nghe / soundcard thì dùng, không thì micro máy"
     "Micro trong máy" — "Kể cả khi đang cắm tai nghe"
     "Micro tai nghe" — "Micro trên dây tai nghe (cổng 3,5 mm hoặc adapter)"
     "Soundcard / micro USB" — "Cắm qua cáp OTG / cổng Lightning, USB-C"
   - Caption: "Đang dùng: Micro trong máy".
```

### Prompt 18 — Hộp thoại và báo lỗi

```
Using the same design system, create a set of dialogs and states:
1) Dialog "Bạn vừa cắm soundcard?" with the text "Thiết bị USB có thể là soundcard hoặc tai nghe USB-C." and the buttons "Dùng chế độ soundcard" / "Không, chỉ là tai nghe".
2) A soft red error card on the Singing screen: "Không mở được bài này. Hãy thử file MP3, M4A hoặc WAV khác.", with a small text button "Chi tiết".
3) A warning strip in the Singing bottom bar: "Đang dùng loa ngoài: dễ bị hú. Hãy cắm tai nghe có dây."
4) A snackbar: "Đã gắn lời cho bài "Phố Đêm Lấp Lánh"."
5) A no-network card in the separation sheet: "Tách giọng bị ngắt (mất mạng hoặc app vào nền). Bấm "Tiếp tục" để tách tiếp.", with the button "Tiếp tục tách".
```

### Prompt 19 — Giao diện tối, máy nhỏ chữ to

```
Create dark-mode versions of "Trang chủ", the Singing screen, and "Nghe lại & chỉnh":
- Background #15111A, surfaces #211B26.
- Primary #FF5A64 (slightly brighter rose for contrast); keep the record button red.
Then make a version of the Singing screen on a small iPhone SE (375×667) with the system text size at 130%:
- Nothing truncates.
- The chips wrap to 2 lines.
- The slider labels stay on 1 line.
- The record button stays fully visible.
```

### Prompt 20 — (tuỳ chọn) Thanh tab dưới

```
Create a variant of the Home screen with a bottom tab bar of 4 tabs: "Trang chủ", "Bài hát", "Bản thu", "Cài đặt". Use rounded icons; the active tab is rose-red with a soft pill highlight. Keep the "Thêm bài hát" hero card as the main action.
```

### Prompt 21 — Làm lại Màn hát (sau khi rà thiết kế)

Stitch đã thêm điểm số, tên micro, thông số giả và hai hàng nút. Prompt này làm lại đúng chức năng
của app. Chọn màn "Màn hình Hát - Phố Đêm Lấp Lánh" rồi dán.

```
Redesign this Singing screen. Keep the deep plum stage look, the fonts, and the rounded cards. Use only the elements below, and fit everything except the last 3 items in 390×844.

Remove completely: score, combo, percentages, "Studio AI", "Vocal FX", "Reverb", "Phòng thu trực tiếp", microphone brand, audio quality specs, the user avatar, the delay figure, and the whole bottom navigation ("Chỉnh âm", "Hát lại", "Vocal Cut", "Hoàn tất", and the large microphone button).

Top to bottom:
- App bar: back arrow, full song title "Phố Đêm Lấp Lánh" (allow it to fit, do not truncate), a speaker icon, a "⋯" icon.
- "La thứ (Am)" in big bold text, caption "theo bài".
- Tone stepper: round "−", the label "Tone −1", round "+". Caption: "Đã hạ 1 nửa cung, giữ nguyên nhịp."
- Pitch meter card: big note "Mi4" and the word "Hơi cao" in orange; a track from "Thấp" to "Cao" with a green center zone and a glowing dot.
- Karaoke lyrics: "Gió về ngang sông chiều nay" large with the sung part in rose-red; next line dimmer, "Thuyền ai neo bến đợi chờ".
- "Chỉnh giọng": chips "Tắt", "Tự nhiên" (selected), "Ca sĩ", "Robot".
- "Thể loại": scrollable chips "Khô", "Bolero" (selected), "Ballad", "Nhạc trẻ", "Remix", "Rap", "Lofi". Caption: "Gợi ý theo nhịp 92 BPM".
- Slider "Vang" with "120%".

One fixed bottom bar only: a seek bar with "1:24" and "3:48"; then one row with a rewind-10 icon button, a round play/pause button, a wide solid red pill button "● Thu" (no gradient, the most prominent control), and a camera icon button.
```
