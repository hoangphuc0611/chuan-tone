# ADR-004: Đổi tone beat bằng render nền từ vị trí đang phát

- Trạng thái: Đã chấp nhận (2026-10-26 duyệt cùng kế hoạch M4, điểm 1, 4, 5; cài đặt 2026-10-27,
  C API 0.5)

## Bối cảnh
Đổi tone beat ±6 nửa cung (CLAUDE.md 3.5) dùng phase vocoder FFT 4096 + giữ transient
(`engine/src/stretch/`). Chạy streaming ngay trong audio thread thì tốn CPU liên tục và dồn cục mỗi
hop, đúng trên luồng căng nhất khi M5 thêm FX / reverb (rủi ro 4 của `docs/plan.md`), và phải "mồi"
lại pipeline mỗi lần tua. Spec cho phép "render trước cả bài ở luồng nền để tiết kiệm pin".

## Quyết định
1. **Render ở luồng nền (Worker của engine), bắt đầu từ vị trí đang phát.** `tc_set_transpose(n)`
   tạo `TransposedTrack` (cùng độ dài beat, PCM int16 dư 6 dB, cờ atomic cho từng khối 4096 frame
   ≈ 85 ms) và giao cho `BeatPlayer`; `BeatRenderer` render từ khối chứa vị trí phát tới cuối bài
   rồi lấp phần đầu bài. Mỗi bước 8 khối (~0,7 s audio) rồi tự đăng lại → tua / đổi tone / nạp beat
   chen vào được; yêu cầu cũ bị huỷ giữa hai bước.
2. **Audio thread chỉ đọc PCM + crossfade.** Bản mới được dùng khi khối tại vị trí phát đã công bố
   (release / acquire); trước đó vẫn phát bản đang nghe. Chuyển bằng crossfade công suất đều 20 ms.
   Tone 0 = beat gốc (luôn sẵn) → chuyển ngay, đúng từng mẫu sau crossfade. Tua tới khối chưa có →
   im lặng, vị trí đứng yên tới khi xong; khối sắp tới chưa có (luồng render chậm) → fade ra 5 ms
   trước rồi chờ, không tắt phụt.
3. **Chỗ nối hai lượt render**: lượt bắt đầu ở khối b > 0 render trước 1024 frame ngay trước b
   (không công bố); lượt khác đi tới sát b trộn dần đuôi của nó sang phần đó (raised cosine ~21 ms).
4. **Sở hữu**: beat gốc và bản đổi tone giữ bằng `shared_ptr` ở luồng nền; audio thread chỉ có con
   trỏ thô qua hàng đợi SPSC, trả lại qua hàng đợi khác; luồng nền mới huỷ. Mọi lệnh tới audio
   thread (nạp beat, đổi tone, dọn) đều đi qua Worker → hàng đợi đúng một bên ghi.
5. **Key**: `tc_set_key` và timeline (M3) là tone của beat **gốc**. Khi bản mới bắt đầu được nghe,
   audio thread cộng số nửa cung vào chủ âm của key chỉnh giọng (`VoicePitchNode::setTranspose`) và
   engine phát `TC_EVENT_KEY_CHANGED` (code = chủ âm đã cộng). `tc_get_applied_key` = tone đang nghe;
   `tc_get_key` giữ nguyên ý nghĩa cũ. Tuning không đổi (đổi đúng 2^(n/12)).
6. **Nạp beat mới → tone về 0**; app tự đặt lại tone đã nhớ cho bài. Chưa lưu bản render ra đĩa.
7. **API 0.5**: `tc_set_transpose / tc_get_transpose / tc_get_applied_key`, `TC_EVENT_TRANSPOSE_READY`,
   `tc_playback_state` thêm `transpose`, `audible_transpose`, `transpose_progress` (struct của
   binding 0.4 vẫn được nhận).

## Lý do
- Audio thread gần như không tốn thêm CPU; render một lần cho cả bài (tiết kiệm pin); thuật toán
  được nhìn trước thoải mái (dò onset).
- Đo trên Mac M1 (Release, beat thật 4 phút): bấm đổi tone giữa bài → nghe tone mới sau **7 ms**;
  render xong cả bài 2,2–3,3 s. Kế hoạch đặt ≤ 300 ms.
- Không có callback gọi ngược; mọi trạng thái kéo qua `tc_get_playback_state` / sự kiện (ADR-001).

## Hệ quả
- RAM thêm một bản beat (bài 4 phút stereo 48 kHz ≈ 46 MB; 10 phút ≈ 115 MB). Nếu đo trên Android
  tầm trung thấy căng: chỉ giữ cửa sổ quanh đầu phát (rủi ro 3 của `docs/m4-plan.md`).
- Đỉnh vượt 0 dBFS sau đổi pha (beat gốc đã sát 0 dBFS) không bị cắt trong bản lưu nhưng tầng ra vẫn
  kẹp ±1 như beat gốc — limiter của M5 xử lý chung.
- Hai lượt render bắt đầu ở hai chỗ khác nhau có pha các thành phần ngân lệch nhau: ở chỗ nối có thể
  hơi hụt mức trong ~10 ms (chỉ xảy ra một lần mỗi chỗ nối, khi đổi tone giữa bài rồi tua lùi qua chỗ
  đó). Nghe kiểm ở B7.
