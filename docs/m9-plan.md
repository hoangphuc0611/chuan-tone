# M9 — Tối ưu, đo trên máy thật, chuẩn bị phát hành

> Bạn duyệt ngày 2026-11-20 ("duyệt plan và thực hiện cho tôi"). Mốc M9 (CLAUDE.md mục 9): tối ưu hiệu năng, thử
> trên 10 máy Android phổ biến ở VN + 3 iPhone, chuẩn bị phát hành. Hoàn thành khi đạt toàn bộ mục 7.
>
> M8 chưa đóng (còn B8: 5 người thử trên iPhone). M9a không đụng tới luồng dùng của B8, nên làm
> song song được.

## 1. Mục 7: hiện trạng và cách đo ở M9

| Chỉ tiêu (mục 7, ADR-002) | Đã có | Đo ở M9 | Cần máy |
|---|---|---|---|
| Độ trễ nghe lại: iOS ≤ 15 ms (bật chỉnh giọng ≤ 25 ms) | iPhone 13 Pro loa ngoài 12,6 ms (M1) | Tai nghe có dây trên 3 iPhone | iPhone (M9b) |
| Độ trễ Android flagship ≤ 20 / 30 ms, tầm trung ≤ 30 / 40 ms | chưa đo | ≥ 2 máy rồi chốt chỉ tiêu tầm trung (quyết định 2026-10-21) | Android (M9c) |
| CPU chuỗi giọng ≤ 25% một nhân, máy tầm trung | Mac M1: cả engine 1,3%, FX 0,3% | Công cụ **Kiểm tra máy** trong app (B4) chạy trên từng máy | 13 máy |
| Không xrun / rè trong 30 phút hát | test không cấp phát trong audio callback | Phần DSP: test 30 phút âm thanh trên Mac (B3) + Kiểm tra máy bản dài. Phần I/O: hát 30 phút thật, đọc số xrun | iPhone, Android |
| Dò tone MIREX ≥ 0,85 | 0,864 trên bộ test (M3) | Giữ nguyên. B2 chỉ đổi cách *dùng* timeline, không đổi bộ dò | — |
| Crash-free ≥ 99,5% | — | Nhật ký lỗi + đếm phiên trên máy (B5); tỉ lệ tính ở vòng thử | iPhone, Android |
| Kích thước app ≤ 80 MB | IPA 9 MB (nén) | Đo cỡ cài đặt iOS + APK / AAB từ CI (B6) | — |
| Offline | chỉ tách giọng dùng mạng | Test: không mở kết nối mạng khi chưa bấm tách (B6) | — |
| Không upload khi chưa đồng ý | hộp hỏi trước khi gửi (M6) | Chính sách quyền riêng tư (B6) | — |

## 2. Ba phần

- **M9a — làm ngay, không cần máy** (mục 3): sửa #11 và #12, test 30 phút trên Mac, công cụ Kiểm
  tra máy, nhật ký lỗi, giấy tờ phát hành.
- **M9b — cần iPhone** (bạn làm, tôi hướng dẫn):
  - Đo độ trễ tai nghe có dây và chạy Kiểm tra máy trên 3 iPhone.
  - Hát liên tục 30 phút, đọc số xrun ở Phòng thử.
  - B8 của M8.
- **M9c — cần máy Android**:
  - Đo độ trễ ≥ 2 máy, chốt chỉ tiêu tầm trung.
  - Xuất video trên Android (MediaCodec + MediaMuxer).
  - Kiểm tra máy trên 10 máy Android phổ biến; soundcard OTG.

  Tôi không viết phần xuất video Android khi chưa có máy để thử: viết mò rồi chỉ build được trong
  CI thì dễ hỏng mà không ai biết. Khi có ít nhất 1 máy Android thì làm.

## 3. Việc M9a

### B1. #11: trạng thái phát đúng ngay khi nạp bài xong (engine 0.9.1)

**Lỗi.** `TC_EVENT_BEAT_LOADED` được gửi lúc luồng nền vừa đưa bài cho bộ phát. Luồng âm thanh chỉ
nhận bài ở khối kế tiếp. Trong khoảng đó có hai cái hỏng:
- `audible_transpose` và `tc_get_key` vẫn còn số nửa cung của bài cũ. Màn hát có thể hiện tone sai
  một lúc ngắn.
- Khối âm thanh của bài cũ đang chạy dở có thể ghi đè vị trí phát (vừa về 0) bằng vị trí cũ, và
  có thể bật cờ "hết bài" của bài cũ cho bài mới.

**Sửa.** Bộ phát đánh số thế hệ cho mỗi lần đổi bài. Luồng âm thanh ghi lại thế hệ đã nhận. Khi hai
số còn lệch (bài mới chưa được nhận), các hàm đọc trả về trạng thái của bài mới:
- vị trí 0 (hoặc chỗ vừa tua tới);
- 0 nửa cung, không chờ khối;
- không có cờ "hết bài".

Lúc nhận bài, luồng âm thanh xoá cờ cũ rồi mới công bố thế hệ mới. Không khoá, không cấp phát.
Giữ nguyên thời điểm gửi sự kiện, để test và chế độ engine dừng không bị treo chờ.

**Test.**
- Test tuần tự.
- Test hai luồng: luồng âm thanh chạy liên tục, đổi bài 2000 lần; ngay sau mỗi lần đổi, vị trí
  phải là 0 và số nửa cung phải là 0.

**Phiên bản.** Engine lên 0.9.1. Các bài đã phân tích sẽ được phân tích lại một lần (cache theo
phiên bản engine, khoảng 2 s mỗi bài).

### B2. #12: không theo tone của đoạn ngắn ở đầu / cuối bài

Ví dụ: "Lạc trôi" có đoạn dạo 18 s bị dò thành La# trưởng (độ tin cậy 0,47). Màn hát hiện tone đó,
và chỉnh giọng theo nó, rồi mới đổi sang La thứ.

Từ M8, phần hiển thị "chuyển tone" đã bỏ đoạn đầu / cuối ngắn hơn 20 s. Giờ dùng **một hàm chung**
cho mọi chỗ dùng timeline: Màn hát, engine (chỉnh giọng theo bài), Nghe lại, dòng chuyển tone.
- Đoạn đầu / cuối ngắn hơn 20 s được gộp vào đoạn kề bên.
- Bộ dò giữ nguyên, nên điểm MIREX của M3 không đổi và bộ test không phải chấm lại.

Test bằng timeline giả, kèm đúng ca "Lạc trôi".

### B3. Test "30 phút hát" trên Mac (phần DSP)

Test engine chạy 30 phút âm thanh qua cả chuỗi bằng `tc_engine_process_offline`.

Đầu vào:
- giọng giả có vibrato, lướt nốt, hơi thở / khoảng lặng;
- beat;
- bật chỉnh giọng;
- 30 s đổi preset thể loại một lần, đổi tone beat vài lần, đổi mức chỉnh giọng.

Kiểm tra:
- không NaN / Inf;
- đầu ra không vượt trần limiter;
- không cấp phát trong audio callback;
- thời gian xử lý mỗi khối không tăng dần (5 phút cuối so với 5 phút đầu, p99);
- vị trí phát chạy đúng.

Test này chạy bản Release (khoảng 30 s), không chạy bản Debug. CI chạy ở bước riêng.

### B4. Công cụ "Kiểm tra máy" (Cài đặt → Nâng cao)

Dùng cho 13 máy ở M9b / M9c: người thử chỉ cần bấm nút rồi gửi báo cáo, không cần máy tính.

**Cách chạy.** App tạo một engine phụ không có thiết bị âm thanh, đẩy giọng giả + beat qua cả chuỗi
(chỉnh giọng Ca sĩ, preset Bolero) nhanh hết mức, rồi đo thời gian. Có hai mức:
- **nhanh**: 30 s âm thanh;
- **dài**: 30 phút âm thanh, trên iPhone khoảng 1 phút.

**Báo cáo gồm:**
- đời máy, hệ điều hành;
- CPU trung bình và p99 (% một nhân), số lần nhanh hơn thời gian thực;
- kiểm NaN / trần;
- độ trễ đã đo, số xrun của phiên hát hiện tại;
- số phiên và số lần thoát bất thường (B5).

Gửi bằng bảng chia sẻ (Zalo, mail…).

**Đạt** khi CPU p99 ≤ 25% một nhân. Đây là chỉ tiêu mục 7 tính cho riêng chuỗi giọng, đo không có
I/O.

### B5. Nhật ký lỗi và đếm phiên trên máy (không dùng dịch vụ ngoài)

**Ghi.** Lỗi Dart chưa bắt (`FlutterError.onError`, `PlatformDispatcher.onError`) được ghi vào file
trong thư mục app: giới hạn 200 KB, quay vòng.

**Đếm phiên.**
- Khi app ra trước màn hình thì đặt cờ "đang chạy". Khi app vào nền thì xoá cờ.
- Mở app mà cờ còn nghĩa là lần trước thoát bất thường khi đang dùng (crash, hoặc bị hệ điều hành
  giết khi đang mở).
- Cách này cho số crash-free ở vòng thử, không cần Crashlytics / Sentry.

**Gửi.** Cài đặt → "Gửi nhật ký lỗi" (bảng chia sẻ), chỉ hiện khi có nội dung. Không tự gửi gì
lên mạng.

**Giới hạn.** Crash C++ / native không qua được Dart. Phần này ghi ở hướng dẫn: iOS → Cài đặt →
Quyền riêng tư → Phân tích → Dữ liệu phân tích → `Runner-…`. Báo crash tự động là quyết định của
bạn (mục 5).

### B6. Giấy tờ và kiểm tra trước phát hành

Tài liệu:
- `docs/release-checklist.md`: những gì cần cho App Store / Google Play (tài khoản, ký app, nhãn
  quyền riêng tư, Data safety, ảnh màn hình, phân loại tuổi, phiên bản / số build, giữ ký hiệu gỡ
  lỗi).
- `docs/privacy-policy.md`: bản nháp tiếng Việt, đúng như app đang làm. Dữ liệu ở trên máy; tách
  giọng chỉ gửi khi đồng ý, xoá sau 24 giờ. Chưa có máy chủ thật nên chưa có địa chỉ đăng.
- `docs/store-listing.md`: tên, phụ đề, mô tả, từ khoá — không dùng "Auto-Tune".

Kiểm tra tự động:
- Script đối chiếu gói Dart trong `pubspec.lock` với `docs/licenses.md` và chặn license ngoài danh
  sách cho phép. Chạy trong CI.
- Đo kích thước bản cài iOS và APK trong CI, ghi vào báo cáo.

Cập nhật:
- `docs/patent-review.md` theo cách làm hiện tại (M2–M8).

### B7. Đổi tone beat +4…+6 đỡ "phơ" — thử, không bắt buộc

Việc còn lại của M4 (`docs/stretch-results.md`): thử cửa sổ ngắn hơn và đổi cửa sổ quanh tiếng trống
khi nâng tone. Chỉ giữ nếu số đo tốt hơn **và** bạn chọn bản mới khi nghe A/B mù trên trang nghe
cục bộ (không đăng). Làm sau cùng; hết thời gian thì để lại.

## 4. Mặc định tôi tự chọn (bạn đổi được)

- Không làm "chế độ nhẹ" cho máy yếu (reverb 4 dòng…): cả engine chỉ 1,3% một nhân trên M1, máy
  tầm trung dự kiến < 10%. Chỉ làm nếu Kiểm tra máy cho thấy có máy vượt 25%.
- #11 sửa ở engine, không đổi C API: app không phải sửa gì.
- #12 sửa ở app (cách dùng timeline), không đổi bộ dò tone.
- Nhật ký lỗi chỉ nằm trên máy. Người dùng tự bấm gửi.

## 5. Cần bạn quyết trước khi phát hành (không chặn M9a)

1. **Tách giọng khi phát hành.** Trọng số htdemucs chỉ "cho mục đích khoa học" (M6). Có ba hướng:
   - (a) API tách trả phí;
   - (b) xin giấy phép Meta;
   - (c) tự huấn luyện model.

   Nếu chưa chốt được, có thể phát hành bản 1 **không có tách giọng**: không có máy chủ thì nút đã
   tự ẩn. Kèm theo là máy chủ thật (GPU, HTTPS, tên miền) và chi phí.
2. **Báo crash tự động** (Crashlytics / Sentry / không dùng): liên quan quyền riêng tư và chính
   sách.
3. **Tài khoản phát hành.** Apple Developer (99 USD/năm), Google Play Console (25 USD). Khoá ký
   Android do bạn giữ.
4. **Máy thử.** Danh sách 10 máy Android phổ biến ở VN và 3 iPhone, mượn hay mua.

## 6. Ngoài phạm vi M9

Chấm điểm, song ca, livestream, tài khoản / thanh toán, kho tone dùng chung + fingerprint, model tách
trên máy (giai đoạn 2 mục 4).

## 7. Rủi ro

| # | Rủi ro | Cách giảm |
|---|---|---|
| 1 | Sửa #11 làm hỏng đổi tone / tua đang chạy tốt | Giữ nguyên thời điểm sự kiện và thứ tự lệnh; test cũ + test hai luồng mới; chạy walkthrough |
| 2 | Kiểm tra máy đo không giống lúc hát thật (không có I/O) | Ghi rõ trong báo cáo; xrun đo riêng bằng 30 phút hát thật |
| 3 | Gộp đoạn đầu / cuối bỏ mất một lần chuyển tone thật | Chỉ gộp đoạn < 20 s ở hai đầu bài (như phần hiển thị M8); đoạn giữa giữ nguyên |
| 4 | Thiếu máy Android làm M9 kéo dài | M9c tách riêng; M9a / M9b không phụ thuộc |

## 8. Các bước

| Bước | Nội dung | Ước (ngày) |
|---|---|---|
| B1 | #11 engine 0.9.1 + test hai luồng | 0,5 |
| B2 | #12 timeline dùng chung | 0,25 |
| B3 | Test 30 phút trên Mac + bước CI | 0,5 |
| B4 | Kiểm tra máy (engine phụ, báo cáo, chia sẻ) | 1 |
| B5 | Nhật ký lỗi + đếm phiên | 0,5 |
| B6 | Giấy tờ phát hành, kiểm license, đo kích thước | 0,75 |
| B7 | (thử) đổi tone +4…+6 | 1 |

Mỗi bước: commit + push + CI xanh. Cuối M9a: báo cáo số đo + IPA mới + hướng dẫn M9b.

## 9. Tiến độ

- [ ] B1 · [ ] B2 · [ ] B3 · [ ] B4 · [ ] B5 · [ ] B6 · [ ] B7
