# Thử trên máy thật — M9b (iPhone) và M9c (Android)

> Cách làm cho người thử; không cần máy tính. Kết quả gửi về được điền vào `docs/m9-results.md`.
> Kế hoạch: `docs/m9-plan.md`.

## Chuẩn bị

- Cài bản mới nhất. iPhone: IPA từ CI qua Sideloadly. Android: APK, khi có.
- Sạc pin trên 50%. Tắt chế độ nguồn điện thấp / tiết kiệm pin.
- Tai nghe **có dây** (jack 3,5 mm hoặc USB-C / Lightning có dây).

## Bước 1 — Kiểm tra máy (5 phút)

1. Cài đặt → Nâng cao → **Kiểm tra máy** → **Kiểm tra nhanh**. Đợi vài giây.
2. Bấm **Kiểm tra lâu**. Để máy yên khoảng 1–2 phút (không tắt màn hình, không mở app khác).
3. Bấm **Gửi kết quả** → gửi qua Zalo / mail cho nhóm phát triển.

Đạt khi CPU trung bình ≤ 25% một nhân (CLAUDE.md mục 7) và "Mẫu lỗi" = 0.

## Bước 2 — Độ trễ (5 phút)

1. Cài đặt → Âm thanh → **Đo độ trễ**. Làm theo hướng dẫn: đo qua loa trước.
2. Tai nghe có dây **có micro trên dây** (vd EarPods): cắm vào, mở Cài đặt → Nâng cao → **Đo độ trễ
   (kỹ thuật)**. Đặt một bên tai nghe sát micro trên dây, bấm đo; nếu app nhắc rút tai nghe thì
   chọn "Vẫn đo với thiết bị này". Tai nghe không có micro: bỏ qua bước này (app tự suy độ trễ tai
   nghe từ số đo loa).
3. Bấm **Sao chép kết quả** ở màn đo kỹ thuật → dán gửi.

Chỉ tiêu (ADR-002):

| Máy | Tắt chỉnh giọng | Bật chỉnh giọng |
|---|---|---|
| iPhone | ≤ 15 ms | ≤ 25 ms |
| Android flagship | ≤ 20 ms | ≤ 30 ms |
| Android tầm trung | ≤ 30 ms | ≤ 40 ms (chốt sau khi đo ≥ 2 máy) |

Bật chỉnh giọng thì cộng thêm 10 ms của khối chỉnh giọng và 1 ms của limiter.

## Bước 3 — Hát 30 phút (35 phút)

1. Cắm tai nghe có dây. Mở một bài dài (bài mẫu hoặc bài của bạn), bật chỉnh giọng **Ca sĩ**,
   thể loại **Bolero**.
2. Hát (hoặc mở nhạc có lời gần micro) liên tục **30 phút**. Đổi bài, đổi tone, đổi thể loại vài lần
   như dùng thật.
3. Trong lúc hát, để ý tiếng **rè / lụp bụp / mất tiếng**. Ghi lại phút nào bị.
4. Xong: Cài đặt → Nâng cao → **Phòng thử** → đọc dòng **Xrun (rè)**. Chụp màn hình.
5. Cài đặt → Nâng cao → **Nhật ký lỗi** → gửi.

Đạt khi không nghe rè và số xrun không tăng trong lúc hát (mục 7). Ghi kèm máy có nóng không.

## Bước 4 — Dùng thường ngày (cả đợt thử)

Dùng app như bình thường. Cuối đợt, gửi **Nhật ký lỗi** (Cài đặt → Nâng cao). Dòng tóm tắt "N phiên
· M lần thoát bất thường" cho tỉ lệ crash-free; chỉ tiêu ≥ 99,5%.

## Danh sách máy

| # | Máy | Loại | Ai giữ | Bước 1 | Bước 2 | Bước 3 |
|---|---|---|---|---|---|---|
| 1 | iPhone 13 Pro (iPhone14,2) | iPhone | bạn | ⬜ | loa ngoài 12,6 ms (M1); tai nghe ⬜ | ⬜ |
| 2 | iPhone … | iPhone | | ⬜ | ⬜ | ⬜ |
| 3 | iPhone … | iPhone | | ⬜ | ⬜ | ⬜ |
| 4–13 | 10 máy Android phổ biến ở VN (bạn chọn, ví dụ Samsung A-series, Xiaomi Redmi, OPPO A…) | Android | | ⬜ | ⬜ | ⬜ |
