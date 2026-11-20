# Nội dung trang cửa hàng (BẢN NHÁP)

> M9 B6. Chữ cho App Store / Google Play.
>
> Quy tắc:
> - Không dùng "Auto-Tune" / "AutoTune" (thương hiệu Antares).
> - Không nêu tên bài hát hay ca sĩ có thật.
> - Không hứa điều app chưa làm (chấm điểm, song ca, livestream, kho bài).
> - Tách giọng ca sĩ chỉ ghi khi đã chốt cách phát hành (`docs/release-checklist.md` mục 1).

## Tên và phụ đề

- **Tên**: ChuẩnTone
- **Phụ đề** (App Store, ≤ 30 ký tự): Bấm bài là hát
- **Mô tả ngắn** (Google Play, ≤ 80 ký tự): Hát karaoke tự dò tone, tự chỉnh giọng đúng tone — hát hay ngay.

## Mô tả

Chọn bài là hát hay ngay — không cần máy tính, soundcard hay kiến thức âm thanh.

**Tự dò tone bài hát**
Thêm bài từ máy hoặc gửi từ Zalo. ChuẩnTone tự dò tone và nhịp của bài, kể cả bài chuyển tone giữa
chừng.

**Chỉnh giọng đúng tone**
App tự kéo giọng bạn về đúng nốt theo tone của bài, có ba mức:
- Tự nhiên: giữ chất giọng, chỉnh nhẹ.
- Ca sĩ: tròn nốt như bản thu phòng thu.
- Robot: hiệu ứng giọng máy.

**Đổi tone nhạc ±6**
Bài cao quá hay thấp quá? Hạ hoặc nâng tone nhạc, nhịp giữ nguyên, chỉnh giọng tự theo tone mới.

**Hiệu ứng theo thể loại**
Bolero, Ballad, Nhạc trẻ, Remix, Rap, Lofi. Mỗi thể loại có vang, vọng, EQ chỉnh sẵn, bấm một lần là
xong. Nút "Vang" để chỉnh nhanh.

**Thu âm và quay video** *(Google Play: bỏ phần video tới khi xong xuất video Android — M9c)*
Thu giọng khi hát, nghe lại, đổi hiệu ứng sau khi thu, cắt đầu đuôi. Xuất video dọc 9:16 (camera
trước hoặc nền sóng nhạc), lưu vào Ảnh hoặc chia sẻ lên TikTok, Facebook, Zalo.

**Lời bài hát chạy chữ**
Thêm file lời `.lrc` của bạn, lời chạy theo nhạc.

**Đồng hồ cao độ**
Thấy ngay mình đang hát cao hay thấp hơn nốt.

**Dùng không cần mạng**
Hát, chỉnh giọng, thu âm, xuất video đều chạy ngay trên máy. Bài hát và bản thu của bạn nằm trên máy
của bạn.

Dùng tốt nhất với tai nghe có dây. Tai nghe Bluetooth có độ trễ nên app tự tắt nghe lại giọng.

*ChuẩnTone không kèm bài hát. Bạn dùng các bài mà bạn có quyền sử dụng.*

## Từ khoá (App Store, ≤ 100 ký tự, cách nhau bằng dấu phẩy)

karaoke,hát,chỉnh giọng,tone,dò tone,bolero,ballad,thu âm,giọng hát,micro,beat,hạ tone,tiktok

## Danh mục

Âm nhạc (Music). Phân loại tuổi dự kiến 4+.

## Ảnh màn hình (gợi ý thứ tự)

Lấy từ lượt chạy tự động (`app/test/walkthrough_test.dart`). Cần thêm cấu hình máy cỡ 6,9".

1. Màn hát nền sân khấu: tone to, chip chỉnh giọng — "Tự dò tone, tự chỉnh giọng"
2. Phân tích bài: "Tone: La thứ (Am) – 92 BPM" — "Bấm bài là biết tone"
3. Đổi tone −1 — "Hạ tone, nhịp giữ nguyên"
4. Thể loại Bolero + Vang — "Hiệu ứng chỉnh sẵn theo thể loại"
5. Nghe lại với hình sóng — "Thu xong chỉnh lại thoải mái"
6. Video 9:16 — "Xuất video, đăng TikTok"

Ảnh chỉ dùng bài mẫu tự tạo của app, không dùng bài có bản quyền.
