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

### 1.0 Lượt chạy tự động (máy làm trước, không cần iPhone)

`app/test/walkthrough_test.dart` chạy app thật (engine thật backend NULL, micro giả hát lệch nốt
+ tiếng loa lọt vào) như một người thử mới. Nó đi qua hướng dẫn 6 bước, rồi nhận bài "Mở bằng
ChuẩnTone" kèm lời gửi riêng, rồi Phân tích bài. Ở Màn hát nó xem mẹo lần đầu, chọn chỉnh giọng
Tự nhiên, hạ tone, chọn Bolero và tăng Vang. Sau đó nó thu 30 giây và sang Nghe lại: cho nhạc
nhỏ đi, xuất video, lưu Ảnh, xuất M4A thật. Cuối cùng nó về Trang chủ, mở lại bài và bản thu,
rồi vào Cài đặt. Lượt này chạy trên hai máy: iPhone 13 sáng và iPhone SE tối với chữ 130%. Tràn
bố cục làm test hỏng. Test chạy trong CI mỗi lần push (bài giả).

Trên máy dev có thể chụp màn hình từng bước kèm nhật ký thử nghiệm của lượt, và chạy với bài thật
và tách giọng bằng server thật:

```
cd app && TONECORE_LIB=$PWD/../engine/build/host/libtonecore.dylib \
  WALKTHROUGH_OUT=$PWD/../testdata/walkthrough \
  WALKTHROUGH_SONG=$PWD/../testdata/vocal-songs/<bài>.m4a \
  CHUANTONE_SERVER=http://127.0.0.1:8000 \
  fvm flutter test test/walkthrough_test.dart
```

Lượt này **không thay** được người thử: không đo được người ta có tự tìm ra nút hay không, không
có SUS. Nó cũng không chạm phần cứng và native của iPhone: âm thanh thật, độ trễ, tai nghe
Bluetooth, dựng video bằng AVFoundation, camera, TikTok, chạy nền thật của iOS, soundcard. Các mục
K1–K14 dưới đây vẫn cần làm trên máy. Lượt tự động chỉ bắt trước lỗi luồng, lỗi chữ và lỗi bố cục
để buổi thử không tốn thời gian vào chúng.

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

"Tự động" = lượt chạy tự động (mục 1.0), không phải người thử.

| # | Vòng | Màn | Vấn đề | Mức (chặn / nặng / nhẹ) | Sửa ở |
|---|---|---|---|---|---|
| 1 | tự động | Màn hát ← Nghe lại | Nghe lại xong bấm quay về Màn hát thì mất bài: 0:00, không phát, không đổi tone, không thu được. Nguyên nhân: Màn hát nạp lại bài ngay lúc bấm quay về, còn màn Nghe lại chỉ gỡ bản thu khi đóng hẳn (sau hiệu ứng chuyển màn), nên gỡ luôn bài vừa nạp. | nặng | B7 (đã sửa: đếm lần nạp, Nghe lại chỉ gỡ khi chưa ai nạp gì sau nó) |
| 2 | tự động | Màn hát | Hát hết bài mà không bấm Dừng thì app thu tiếp khoảng lặng, bản thu và video có đoạn im cuối | nhẹ | B7 (đã sửa: hết bài thì tự dừng → Nghe lại) |
| 3 | tự động | Phân tích bài, Nghe lại, Bản thu của tôi | Tên bài kèm đuôi file ("Lạc trôi.m4a") | nhẹ | B7 (đã sửa) |
| 4 | tự động | Phân tích bài | Đang dò nhịp thì bước "Dò tone" (đã xong) hiện vòng tròn rỗng như chưa làm | nhẹ | B7 (đã sửa) |
| 5 | tự động | Nghe lại | "Đã bù độ trễ 1.0 ms": dấu chấm thập phân | nhẹ | B7 (đã sửa) |
| 6 | tự động | Cài đặt | Hiện "ToneCore v0.8.0" dù engine đã là 0.9 (số phiên bản trong CMake chưa nâng ở B4) | nhẹ | B7 (đã sửa; phân tích bài cũ chạy lại một lần) |
| 7 | tự động | Màn hát | Mở bài dài (vd quay về từ Nghe lại): trong 1–3 giây nạp bài, màn vẫn hiện tone và thời lượng của bản thu vừa nghe | nhẹ | B7 (đã sửa: đang nạp thì hiện "Đang mở bài…", vị trí 0:00) |
| 8 | tự động | Nghe lại | Nút Xuất ở cuối trang, sau khoảng 1,5 màn hình nút chỉnh; xuất xong, thẻ kết quả nằm dưới mép màn hình — người thử có thể không thấy (nhiệm vụ 5) | ? | B7 (đã sửa: nút xuất video chính cố định ở đáy màn; xuất xong tự cuộn tới kết quả / lỗi) — người thử xác nhận |
| 9 | tự động | Nghe lại, Màn hát | Chữ kỹ thuật: "nén 3:1", "−14,0 LUFS", "(hệ điều hành báo)", "theo bài · nhạc −1" | nhẹ | B7 (đã sửa: tả thể loại bằng lời thường, bỏ LUFS và nguồn số đo độ trễ, bỏ "nhạc −1" trùng với Tone −1; thông số vẫn ở Phòng thử) |
| 10 | tự động | Màn hát / Nghe lại | Cùng một thứ mà hai màn gọi khác nhau: "Chỉnh giọng" / "Tự chỉnh giọng", "Thể loại" / "Hiệu ứng giọng"; một màn dùng chip, màn kia dùng nút phân đoạn (SUS câu 6) | nhẹ | B7 (đã sửa: Nghe lại dùng "Chỉnh giọng" + "Thể loại", cùng kiểu chip, cùng thứ tự với Màn hát) |
| 11 | tự động | engine | Engine báo "nạp bài xong" trước khi luồng âm thanh nhận bài: đọc thời lượng hay đổi tone ngay sau đó có thể hụt. Trên máy gần như không xảy ra vì app đọc CSDL xen giữa. | nhẹ | M9 |
| 12 | tự động | Màn hát | "Lạc trôi": đoạn dạo 18 giây bị dò thành La# trưởng (độ tin cậy 0,47) nên Màn hát hiện tone sai đầu bài rồi mới đổi sang La thứ. Có thể chỉ hiện đoạn chuyển tone đủ tin cậy. | nhẹ | xem ở vòng thử / M9 |
| 13 | tự động | Màn hát (bảng Tách giọng) | Nhãn "Giữ giọng ca sĩ gốc" xuống dòng thành 2 dòng ở cột nhãn hẹp | nhẹ | — |
| 14 | tự động | Phân tích bài | iPhone SE, chữ to: thông báo "Đã gắn lời cho bài…" che nút "Hát" trong 4 giây | nhẹ | xem ở vòng thử |
