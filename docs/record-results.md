# Kết quả thu âm, render lại, xuất (M7)

Kế hoạch: `docs/m7-plan.md`. Thiết kế: ADR-007 (ghi thô, bù trễ lúc đọc, căn hình – tiếng theo đồng
hồ máy).

## Chỉ tiêu (m7-plan mục 4)

| Mục | Chỉ tiêu | Kết quả |
|---|---|---|
| Giọng khớp beat sau bù trễ | lệch ≤ 5 ms | engine: đúng R + L (± 2 frame, mô phỏng); iPhone: chờ đo (B2) |
| Không rơi mẫu | 0 frame rơi | test: ring đầy mới rơi (bỏ nguyên khối); iPhone: B7 |
| Render lại bài 4 phút | ≤ 10 s | Mac M1: **4,2 s** (B3b); iPhone: B7 |
| Loudness file xuất | −14 ± 0,5 LUFS, ≤ −1 dBTP | test: −14,0 LUFS, −2,2 dBTP (bài 4 phút) |

## B1–B2 — bộ thu, căn giọng với beat (2026-11-02)

- Ba file cùng frame đầu: giọng khô = micro **từng bit**; beat = file beat từ vị trí bắt đầu (lệch
  ≤ 1 bậc 16-bit); đầu ra engine ở frame n + L = beat[n] + giọng đã xử lý[n] (L = 48 frame = limiter
  1 ms) — `engine/tests/unit/record_test.cpp`.
- Vòng kín mô phỏng loa → micro (`app/test/sync_e2e_test.dart`, engine thật, micro = đầu ra trễ
  600 frame): công cụ "Kiểm tra khớp tiếng" đo giọng khô chậm beat **648 frame = 600 + 48**, lệch
  ≤ 2 frame → công thức bù R + L đúng.
- Trên iPhone (loa ngoài, tai nghe có dây): chờ đo bằng thẻ "Kiểm tra khớp tiếng khi thu" ở màn Đo
  độ trễ.

## B3 — nghe lại, render (2026-11-02)

- Nghe lại (`tc_load_take`): giọng bản thu (xung) ra đúng vị trí beat − độ lệch + L; micro thật bị
  bỏ qua.
- Render (`tc_renderer_*`, Mac M1, Release, `TC_BENCH_RENDER=1`): bản thu **4 phút** (beat + giọng
  đổi nốt mỗi giây), Bolero + chỉnh giọng Ca sĩ + chuẩn hoá: **4,17 s = 58× thời gian thực**, file ra
  **−14,0 LUFS, −2,17 dBTP**.
- Giọng trong file render đúng vị trí như khi nghe lại (xung dịch đúng `voice_offset_frames`, không
  còn trễ limiter); cắt đầu / đuôi đúng độ dài, fade 10 ms.
- Đỉnh thật: sin fs/4 lệch pha 45° đo được 1,00 (đỉnh mẫu 0,707); limiter giữ đỉnh thật ≤ −1 dBTP
  khi đầu vào +2,6 dBTP, dưới trần đi thẳng từng mẫu.
