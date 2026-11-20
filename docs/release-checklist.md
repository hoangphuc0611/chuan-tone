# Danh sách việc trước khi phát hành

> M9 B6 (`docs/m9-plan.md`). Ghi những gì App Store / Google Play cần và hiện trạng của ChuẩnTone.
> Ký hiệu: ✅ đã có · ⬜ chưa làm · 🟡 chờ bạn quyết.

## 1. Quyết định của bạn (chặn phát hành)

| | Việc | Ghi chú |
|---|---|---|
| 🟡 | Tách giọng ca sĩ khi phát hành | Trọng số htdemucs chỉ "cho mục đích khoa học" (M6). Ba hướng: API trả phí / giấy phép Meta / tự huấn luyện. Hoặc phát hành bản 1 **không có tách giọng** (không có máy chủ thì nút tự ẩn) |
| 🟡 | Báo crash tự động (Crashlytics / Sentry / không dùng) | Hiện chỉ có nhật ký lỗi + đếm phiên **trên máy** (M9 B5). Nếu dùng dịch vụ ngoài thì phải sửa chính sách quyền riêng tư và nhãn quyền riêng tư |
| 🟡 | Tài khoản | Apple Developer Program (99 USD/năm), Google Play Console (25 USD, một lần) |
| 🟡 | Máy chủ thật (nếu có tách giọng) | GPU, HTTPS, tên miền, nơi đặt máy chủ (ảnh hưởng chính sách quyền riêng tư) |
| 🟡 | Giá | Miễn phí / freemium (thanh toán ngoài phạm vi MVP) |

## 2. Pháp lý (CLAUDE.md mục 8)

| | Việc | Hiện trạng |
|---|---|---|
| ✅ | Clean-room, nguồn tham khảo | `docs/references.md` |
| ✅ | Danh sách thư viện + license | `docs/licenses.md`. CI kiểm gói Dart (`tools/licenses/check_dart_licenses.py`) |
| ✅ | Không kèm bài hát / beat / lời có bản quyền | Bài mẫu tự tạo bằng code (M8 B3) |
| ✅ | Không có tải nhạc từ YouTube | — |
| ✅ | Không dùng chữ "Auto-Tune" | Tính năng gọi là "Chỉnh giọng" |
| ⬜ | Luật sư rà bằng sáng chế | `docs/patent-review.md` (bản nháp mô tả cách làm M2–M8) |
| ⬜ | Chính sách quyền riêng tư đăng ở địa chỉ công khai | Bản nháp `docs/privacy-policy.md`. Cần trang web / tên miền |
| ⬜ | Điều khoản sử dụng | Chưa soạn. Cần nói rõ người dùng chịu trách nhiệm về bản quyền bài họ nạp |

## 3. App Store (iOS)

| | Việc | Hiện trạng |
|---|---|---|
| ✅ | Câu xin quyền tiếng Việt | Micro, camera, lưu vào Ảnh, mạng cục bộ (`ios/Runner/Info.plist`) |
| ⬜ | Bỏ quyền mạng cục bộ nếu máy chủ tách giọng không còn ở Wi-Fi nhà | `NSLocalNetworkUsageDescription` chỉ cần cho máy chủ trên Mac (M6) |
| ⬜ | `ITSAppUsesNonExemptEncryption = NO` | Chỉ dùng HTTPS của hệ điều hành; thêm để không bị hỏi mỗi lần tải lên |
| ⬜ | Privacy manifest của app (`PrivacyInfo.xcprivacy`) | Plugin `chuantone_media` đã có; app chưa có. Kiểm các "required reason API" (ngày giờ file, dung lượng ổ đĩa, UserDefaults) |
| ⬜ | Nhãn quyền riêng tư (App Privacy) | Không có tách giọng: "Không thu thập dữ liệu". Có tách giọng: "Âm thanh — chức năng app, không gắn danh tính, không theo dõi" |
| ⬜ | Ký app (chứng chỉ phân phối, provisioning) | Hiện chỉ có IPA chưa ký để thử (Sideloadly) |
| ⬜ | Số phiên bản / số build | `app/pubspec.yaml` đang là `1.0.0+1`. CI nên tự tăng số build |
| ⬜ | Giữ ký hiệu gỡ lỗi (dSYM, `--split-debug-info`) | Để đọc được báo cáo crash |
| ⬜ | Ảnh màn hình 6,9" (1320 × 2868) và 6,5" | Lượt chạy tự động chụp được. Cần thêm cấu hình máy cỡ đó (hiện có iPhone 13 và SE) |
| ⬜ | Phân loại tuổi | Dự kiến 4+ (không có nội dung do người khác đăng trong app) |
| ⬜ | TestFlight cho vòng thử trước khi phát hành | Thay Sideloadly (Apple ID miễn phí hết hạn sau 7 ngày) |

## 4. Google Play (Android)

| | Việc | Hiện trạng |
|---|---|---|
| ⬜ | Xuất video, camera trên Android | M9c (cần máy Android) |
| ⬜ | Khoá ký release | Đang ký bằng khoá debug (`android/app/build.gradle.kts`). Bạn giữ khoá; dùng Play App Signing |
| ⬜ | Gói AAB (`flutter build appbundle`) | CI đang build APK. Cỡ APK release arm64 ghi ở tóm tắt job `android` |
| ⬜ | Target API mới nhất theo yêu cầu của Play | Kiểm lúc phát hành |
| ⬜ | Data safety form | Như nhãn quyền riêng tư iOS |
| ⬜ | Kiểm R8 / minify bản release trên máy thật | Bài học cũ: `shared_preferences` lỗi JNI ở bản release (`docs/licenses.md`) |

## 5. Chất lượng (CLAUDE.md mục 7)

| | Chỉ tiêu | Hiện trạng |
|---|---|---|
| ✅ | Dò tone ≥ 0,85 | 0,864 (M3) |
| ✅ | Kích thước ≤ 80 MB | CI ghi cỡ IPA / app / APK mỗi lần build, quá 80 MB thì lỗi |
| 🟡 | CPU ≤ 25% một nhân | Mac 1,3–2,0%. Kiểm tra máy trên 13 máy (M9b / M9c) |
| 🟡 | Độ trễ | iPhone loa ngoài 12,6 ms. Tai nghe có dây + Android: M9b / M9c |
| 🟡 | Không xrun 30 phút | DSP đạt trên Mac (M9 B3). I/O: hát 30 phút thật trên máy |
| 🟡 | Crash-free ≥ 99,5% | Đếm ở vòng thử (Cài đặt → Nâng cao → Nhật ký lỗi) |
| ✅ | Offline | Chỉ tách giọng dùng mạng |
| ✅ | Không upload khi chưa đồng ý | Hộp hỏi trước khi gửi (M6) |
