# Kết quả bộ FX giọng (M5)

> Tình trạng 2026-10-28: xong B1 (EQ, gate, tiện ích đo, `fxlab`). Kế hoạch: `docs/m5-plan.md`.

Chỉ tiêu M5 (CLAUDE.md mục 9): **demo đổi preset khi đang hát** — không click, không hụt tiếng.

## B1 — EQ (SVF dạng TPT) và gate

**SVF so với RBJ cookbook** (`Svf.MatchesRbjCookbook`). Có 12 bộ thông số:
- thông cao 80 / 150 Hz;
- thông thấp 9 / 2,5 kHz;
- bell ±2 … ±12 dB, Q 0,3 … 4;
- low shelf / high shelf ±2 … ±9 dB.

Đo ở 44,1 và 48 kHz, tại 48 tần số từ 20 Hz tới 20 kHz:

| So sánh | Lệch lớn nhất cho phép | Kết quả |
|---|---|---|
| Công thức đáp ứng SVF ↔ RBJ | 0,01 dB | ✅ |
| Đo trên đáp ứng xung (float) ↔ RBJ | 0,1 dB | ✅ |
| EQ 6 tầng nối tiếp (preset Bolero + thông thấp) ↔ tích RBJ | 0,1 dB | ✅ |
| `fxlab response --hpf 80 --bell 300,-2,1 --bell 3000,2,1` | — | 0,000 dB |

HPF giọng 80 Hz: −3,01 dB tại 80 Hz, ≤ −23,5 dB tại 20 Hz, phẳng (±0,01 dB) ở 3 kHz. Mọi băng tắt
hoặc 0 dB thì đi thẳng, đúng từng mẫu.

**Đổi thông số khi đang chạy.** Đo năng lượng trên 12 kHz so với tổng, đầu vào sin 300 Hz,
thông số đặt lại mỗi 10 ms:

| Phép thử | Biquad (TDF-II), hệ số nhảy | SVF, hệ số nhảy | SVF, trượt τ 20 ms |
|---|---|---|---|
| Kéo thanh trượt (200 Hz → 8 kHz, −12 → +12 dB trong 2 s) | −81,5 dB | −78,3 dB | −81,5 dB |
| Nhảy mạnh qua lại (200 Hz −12 dB ↔ 8 kHz +12 dB) | −44,4 dB | −35,8 dB | **−70,7 dB** |

Cách đọc:
- Điều khử tiếng "lụp bụp" là **làm mượt thông số**, không phải bản thân SVF.
- SVF được chọn vì hệ số trượt liên tục (tính lại mỗi 16 mẫu) vẫn ổn định (Zavalishin, ch. 4).
  Biquad dạng trực tiếp không bảo đảm điều đó khi điều chế nhanh.
- Test `Equalizer.SmoothingRemovesClicksOnJumps` giữ cả hai vế: có làm mượt ≤ −60 dB, không làm
  mượt > −50 dB (phép đo đủ nhạy).

**Gate** (`NoiseGate.*`, ngưỡng −50 dBFS):

| Hành vi | Kết quả |
|---|---|
| Mở khi tiếng to hơn ngưỡng 10 dB | hệ số tới −1 dB sau ≤ 3 ms |
| Tiếng dưới ngưỡng 10 dB | đóng, giảm đúng −30 dB |
| Dao động quanh ngưỡng ±2 dB (3 Hz, 2 s) | không đóng lần nào (trễ 6 dB) |
| Tắt tiếng | mở trọn tới khi bao biên xuống dưới ngưỡng đóng + giữ 80 ms (±3 ms), rồi nhả một cực τ 150 ms (±0,01) |

**CPU** (`tonecore_bench`, Mac M1, bản Release, mono 48 kHz, khối 128 mẫu):

| Khối | × thời gian thực | % một nhân |
|---|---|---|
| EQ đủ 7 tầng, thông số đứng yên | 863× | 0,12% |
| EQ, thông số đang trượt | 488× | 0,20% |
| Gate | 1 835× | 0,05% |

Xử lý cả khối trong biến cục bộ (`Svf::processBlock`) nhanh gấp 3,9 lần gọi từng mẫu. Lý do:
trình biên dịch không phải đọc lại hệ số / trạng thái sau mỗi lần ghi qua con trỏ.

**Công cụ:** `fxlab render <file> -o out.wav [--gate dB] [--hpf Hz] [--lpf Hz] [--bell f,dB,Q] …`
(in độ to LUFS vào / ra) và `fxlab response …` (bảng đáp ứng đo / RBJ). Giọng nghe thử:
vocadito (CC BY 4.0) trong `testdata/Audio/`.
