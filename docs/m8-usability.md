# M8 – Thử với người dùng: kịch bản, phiếu ghi, phiếu SUS

> Kèm `docs/m8-plan.md` mục 6 (chỉ tiêu) và mục 7 (thử với người dùng). Vòng nhỏ 1–2 người ở B7 để
> bắt lỗi lớn (gộp phần thử iPhone còn treo của M7), sửa xong mới thử 5 người chính thức ở B8.
> Nhật ký và ghi chép buổi thử để trong `testdata/usability/` (không commit, không gửi lên mạng).

**Tiêu chí đạt (B8):**
- mỗi nhiệm vụ có ≥ 4/5 người tự làm xong; nhiệm vụ 2 ("thêm bài rồi hát có chỉnh giọng") phải 5/5;
- không có lỗi chặn (crash, mất bản thu) trong mọi buổi thử;
- điểm SUS trung bình ≥ 70;
- từ lúc mở app tới câu hát đầu tiên ≤ 3 phút (gồm hướng dẫn, chưa tính tách giọng).

---

## 1. Vòng nhỏ (B7): bạn + 1–2 người

### 1.1 Trước buổi thử (bạn tự làm, ~30 phút)

1. Cài IPA mới nhất từ CI (`gh run download <id> -n ChuanTone-ipa` → Sideloadly). **Xoá app cũ
   trước** để thấy hướng dẫn lần đầu, hộp hỏi quyền micro, icon và màn khởi động mới.
2. Cài đặt → Nâng cao → bật **Ghi nhật ký thử nghiệm** (mở app lần đầu thì bỏ qua hướng dẫn, bật
   nhật ký, rồi Cài đặt → Trợ giúp → "Xem lại hướng dẫn").
3. Mac chạy máy chủ tách giọng (`uv run chuantone-server`), iPhone cùng Wi-Fi, nhập địa chỉ ở
   Cài đặt → Nâng cao.
4. Đi một lượt các mục kỹ thuật dưới đây, ghi vào cột "Kết quả" (số đo M7 ghi vào
   `docs/record-results.md`).

| # | Kiểm tra | Cách làm | Kết quả |
|---|---|---|---|
| K1 | Hướng dẫn lần đầu, quyền micro | Mở app lần đầu → đủ 6 bước; thử "Không cho phép" micro → có nút "Mở Cài đặt" | |
| K2 | Icon, màn khởi động, giao diện tối | Màn hình chính, mở app; bật Giao diện tối của máy | |
| K3 | Nhận bài từ Zalo / Tệp ("Mở bằng ChuẩnTone") kèm file `.lrc` | Gửi bài + lời cùng tên qua Zalo → Mở bằng ChuẩnTone | |
| K4 | Màn hát | Tone ±, chỉnh giọng, thể loại, Vang, lời chạy chữ, đồng hồ cao độ; mẹo lần đầu chỉ hiện một lần | |
| K5 | Khớp tiếng khi thu (M7 B2) | Phòng thử → kiểm tra khớp tiếng: loa ngoài và tai nghe có dây, lệch ≤ 5 ms | |
| K6 | Thu → Nghe lại → xuất (M7 B4–B5) | Thu 1 phút, đổi Bolero → Nhạc trẻ ở màn Nghe lại, xuất M4A + video sóng nhạc; đo thời gian xuất bài 4 phút | |
| K7 | Video camera (M7 B6) | Thu có camera, vỗ tay trước camera; xuất → hình khớp tiếng (≤ 40 ms) | |
| K8 | Đăng TikTok (M7) | Chia sẻ → TikTok, "Chỉ mình tôi" | |
| K9 | Cài đặt | Đo độ trễ từng bước (loa ngoài → kết quả bằng lời), Micro, chất lượng xuất, Bộ nhớ (xoá từng nhóm) | |
| K10 | Chạy nền khi xuất (B7) | Bấm Xuất video sóng nhạc bài 4 phút → khoá máy 20 giây → mở lại: xuất xong hoặc đang chạy tiếp | |
| K11 | Tắt app khi đang xuất (B7) | Bấm Xuất → vuốt tắt app → mở lại bản thu (Bản thu của tôi): báo "Lần xuất trước bị ngắt… đang xuất lại" và xuất xong | |
| K12 | Tách giọng bị ngắt (B7) | Bấm Tách giọng → chuyển sang app khác 1–2 phút → quay lại: tự tách tiếp; rời Màn hát khi đang tách → Trang chủ có "Đang tách giọng dở" → bấm → tách tiếp | |
| K13 | Soundcard (B6, nếu có máy) | Cắm → hỏi chế độ soundcard; nghe giọng chỉ qua soundcard; đo qua Loopback; thu có Loopback → màn Nghe lại cảnh báo nhạc lọt | |
| K14 | Nhật ký thử nghiệm | Cài đặt → Nâng cao → Chia sẻ nhật ký → AirDrop sang Mac | |

5. Sau đó thử 6 nhiệm vụ (mục 2) với 1–2 người, theo đúng kịch bản như buổi chính thức.

### 1.2 Sau vòng nhỏ

- Ghi lỗi tìm được vào bảng "Vấn đề" cuối file này, mình sửa trước B8.
- Nhật ký: AirDrop vào Mac rồi chép vào `testdata/usability/` (thư mục Tải về của Mac mình không đọc
  được), chạy:

  ```
  python3 tools/usability/task_times.py testdata/usability/nhat-ky-thu-nghiem.jsonl --timeline
  ```

  Công cụ in thời gian từng nhiệm vụ (đoán theo sự kiện), thời gian từ lúc mở app tới câu hát đầu
  tiên, và các lỗi người thử đã thấy. Đồng hồ của người điều phối vẫn là số chính.

---

## 2. Buổi thử chính thức (B8): 5 người

### 2.1 Chuẩn bị

- **Người thử** (không phải bạn bè thân hay người trong nhóm): 18–50 tuổi; ≥ 2 người trên 35
  tuổi; ≥ 2 người không rành công nghệ; ≥ 1 người hát bolero; ≥ 1 người hay livestream / đăng
  TikTok.
- iPhone đã cài bản mới, **xoá app rồi cài lại trước mỗi người** (để thấy hướng dẫn lần đầu), bật
  lại nhật ký thử nghiệm và địa chỉ máy chủ.
- Tai nghe có dây; 1 bài nhạc gửi sẵn qua Zalo (bài có lời ca sĩ, ~3–4 phút); Mac chạy máy chủ tách
  giọng cùng Wi-Fi; đồng hồ bấm giờ; phiếu ghi (mục 2.4) và phiếu SUS (mục 3) in sẵn.
- Mỗi buổi ~30 phút: giới thiệu 3 phút, 6 nhiệm vụ ~20 phút, SUS + hỏi thêm 5 phút.

### 2.2 Lời giới thiệu (đọc cho người thử)

> "Cảm ơn anh / chị đã giúp. Hôm nay mình thử app, không phải thử anh / chị — không có đúng hay sai.
> Chỗ nào khó là lỗi của app, đó chính là điều tụi mình cần biết. Trong lúc làm, anh / chị cứ nói to
> ra mình đang nghĩ gì, đang tìm gì. Mình sẽ không chỉ chỗ bấm, trừ khi anh / chị bí quá lâu. Anh /
> chị dừng lúc nào cũng được."

Người điều phối: không chỉ chỗ bấm. Nếu người thử bí > 2 phút hoặc xin giúp, gợi ý một câu (ghi
"cần gợi ý"); bí > 5 phút thì làm hộ và ghi "bỏ cuộc".

### 2.3 Nhiệm vụ (giao bằng lời, đúng câu chữ)

| # | Lời giao | Xong khi | Gợi ý được phép (ghi "cần gợi ý") |
|---|---|---|---|
| 1 | "Đây là lần đầu anh / chị mở app. Làm theo hướng dẫn tới khi nghe được giọng mình trong tai nghe." | Qua bước "Thử giọng", bấm "Có" / tới bước Xong | "Thử cắm tai nghe vào." |
| 2 | "Em vừa gửi một bài qua Zalo. Anh / chị đưa bài đó vào app rồi hát thử một đoạn, có chỉnh giọng cho đúng nốt." | Bài đang phát, mức chỉnh giọng khác "Tắt" | "Trong Zalo, bấm giữ vào file thử xem." |
| 3 | "Giả sử bài này cao quá với anh / chị. Làm cho bài dễ hát hơn." | Tone đã hạ (−1 trở xuống) | "Nhìn chỗ chữ Tone." |
| 4 | "Giờ anh / chị muốn hát theo kiểu bolero, giọng vang hơn một chút." | Thể loại Bolero, "Vang" > 100% | "Kéo xuống dưới thử." |
| 5 | "Thu khoảng 30 giây, nghe lại, cho tiếng nhạc nhỏ đi, rồi xuất video và lưu vào Ảnh." | Video đã lưu vào Ảnh | "Nút đỏ ở dưới cùng." |
| 6 | "Quay về màn hình đầu tiên. Mở lại bài vừa hát, rồi mở lại bản thu vừa làm." | Đã vào lại Màn hát của bài và màn Nghe lại của bản thu | "Màn hình đầu có mục Hát tiếp." |

### 2.4 Phiếu ghi (một tờ cho mỗi người)

Người thử số: ___ · Tuổi: ___ · Hay hát: ___ · Dùng điện thoại: ☐ rành ☐ bình thường ☐ không rành ·
Ngày giờ: ___

| # | Tự làm xong / Cần gợi ý / Bỏ cuộc | Thời gian (phút:giây) | Chỗ bấm nhầm, chỗ loay hoay | Câu nói đáng chú ý |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |

Lỗi chặn (crash, mất bản thu, treo): ___

Hỏi thêm sau SUS: "Chỗ nào khó nhất?" · "Có chữ nào không hiểu?" · "Anh / chị có dùng app này để hát /
đăng TikTok không, vì sao?"

---

## 3. Phiếu SUS (System Usability Scale, Brooke 1996 — bản dịch tiếng Việt của nhóm)

Người thử khoanh một số cho mỗi câu: **1 = Rất không đồng ý … 5 = Rất đồng ý**. Đọc to nếu người
thử cần. Trả lời theo cảm nhận đầu tiên, không nghĩ lâu.

| # | Câu | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| 1 | Tôi nghĩ mình sẽ muốn dùng app này thường xuyên. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 2 | Tôi thấy app rắc rối một cách không cần thiết. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 3 | Tôi thấy app dễ dùng. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 4 | Tôi nghĩ mình cần người rành kỹ thuật giúp thì mới dùng được app này. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 5 | Tôi thấy các chức năng của app ăn khớp với nhau. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 6 | Tôi thấy app có nhiều chỗ không thống nhất (chỗ này một kiểu, chỗ kia một kiểu). | ☐ | ☐ | ☐ | ☐ | ☐ |
| 7 | Tôi nghĩ hầu hết mọi người sẽ học dùng app này rất nhanh. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 8 | Tôi thấy app rất lóng ngóng, vướng víu khi dùng. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 9 | Tôi thấy rất tự tin khi dùng app. | ☐ | ☐ | ☐ | ☐ | ☐ |
| 10 | Tôi phải học nhiều thứ trước khi dùng được app này. | ☐ | ☐ | ☐ | ☐ | ☐ |

**Tính điểm:** câu lẻ (1, 3, 5, 7, 9): điểm − 1; câu chẵn (2, 4, 6, 8, 10): 5 − điểm; cộng 10
câu rồi nhân 2,5 → 0–100. Trung bình 5 người ≥ 70 là đạt ("tốt" theo Bangor, Kortum, Miller 2009;
trung bình chung các sản phẩm ≈ 68).

---

## 4. Tổng hợp (điền sau mỗi vòng)

| Nhiệm vụ | Tự làm xong | Cần gợi ý | Bỏ cuộc | Thời gian trung vị |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |

SUS trung bình: ___ · Mở app → câu hát đầu tiên (trung vị): ___ · Lỗi chặn: ___

### Vấn đề tìm được

| # | Vòng | Màn | Vấn đề | Mức (chặn / nặng / nhẹ) | Sửa ở |
|---|---|---|---|---|---|
| | | | | | |
