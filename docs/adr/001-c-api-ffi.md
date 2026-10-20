# ADR-001: C API của ToneCore và cách Dart FFI gọi

- Trạng thái: Đã chấp nhận (2026-10-20)

## Bối cảnh
App Flutter cần gọi engine C++ trên Android và iOS. Dart FFI chỉ gọi được hàm C. Engine có audio thread riêng, không được chặn hay gọi ngược vào Dart.

## Quyết định
1. **Một header C duy nhất** `engine/include/tonecore/tonecore.h`, tiền tố `tc_`. Chỉ các hàm này được export (`-fvisibility=hidden`); mã nội bộ nằm trong thư viện tĩnh `tonecore_core`, được gói vào thư viện động `tonecore` (`libtonecore.so` / `tonecore.framework`).
2. **Handle mờ** `tc_engine*`; mọi hàm trả `tc_result`. Không exception nào vượt qua ranh giới C.
3. **Struct cấu hình có `struct_size`** để thêm trường mà không phá ABI. `tc_get_api_version()` cho phép Dart kiểm tra tương thích lúc khởi động (major phải khớp).
4. **Dữ liệu về UI theo kiểu kéo**: Dart gọi `tc_get_metrics` (và `tc_poll_events` từ M1) mỗi khung hình. Kết quả việc dài (phân tích bài) sẽ dùng `NativeCallable.listener` (Dart ≥ 3.1), gọi từ luồng nền — **không bao giờ** từ audio thread.
5. **Binding Dart sinh bằng `ffigen`** từ header; lớp `ToneCore` viết tay bọc lại cho dễ dùng (quản lý bộ nhớ, đổi mã lỗi thành exception Dart).
6. Nạp thư viện: Android `DynamicLibrary.open('libtonecore.so')`; iOS `DynamicLibrary.open('tonecore.framework/tonecore')`; test trên máy dev nạp `engine/build/host/libtonecore.dylib` qua biến môi trường `TONECORE_LIB`.

## Hệ quả
- Đổi C++ nội bộ thoải mái; chỉ header C cần giữ ổn định.
- Mỗi hàm C mới phải có test trong `engine/tests/unit/c_api_test.cpp` và test Dart tương ứng.
