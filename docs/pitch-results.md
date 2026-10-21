# Kết quả dò cao độ & chỉnh giọng (M2)

## Dò cao độ trên giọng hát thật — vocadito (40 bài, 7 ngôn ngữ, CC BY 4.0)

Chạy: `tools/pitch/fetch_datasets.sh && python3 tools/pitch/eval_f0.py --pitchlab engine/build/rel/tools/pitchlab`
(CI chạy lại mỗi lần đổi engine và đòi RPA ≥ 0,90.)

Cấu hình: hạ mẫu về ~16 kHz, cửa sổ 40 ms, hop 4 ms, f0 70–1100 Hz, bộ theo dõi mặc định
(ngưỡng âm lượng −45 dBFS, clarity bắt đầu 0,80 / giữ 0,75, median 3 khung).

| Thuật toán | VR | VFA | RPA | RCA | OA | Sai số trung vị (cent) |
|---|---|---|---|---|---|---|
| MPM | 0.956 | 0.108 | 0.974 | 0.983 | 0.922 | 4.5 |
| YIN | 0.954 | 0.104 | 0.970 | 0.983 | 0.919 | 4.5 |

Bài có RPA thấp nhất (MPM): vocadito_34 (0.88), vocadito_21 (0.91), vocadito_30 (0.93), vocadito_26 (0.94), vocadito_2 (0.94).

**Chọn MPM** cho chuỗi real-time: chính xác ngang/nhỉnh hơn YIN, rẻ hơn nhiều (NSDF qua FFT
1024 điểm, so với YIN tính trực tiếp O(W·τmax)), và có sẵn "clarity" làm độ tin cậy.

Chỉnh ngưỡng hữu thanh (MPM, cùng bộ dữ liệu):

| silenceDb | clarityOff | VR | VFA | OA |
|---|---|---|---|---|
| −55 | 0,60 | 0.972 | 0.182 | 0.907 |
| −45 | 0,60 | 0.966 | 0.140 | 0.917 |
| −55 | 0,75 | 0.962 | 0.137 | 0.917 |
| **−45** | **0,75** | **0.956** | **0.108** | **0.922** |
| −40 | 0,80 (on 0,85) | 0.911 | 0.075 | 0.906 |

## Dò cao độ trên giọng tổng hợp (unit test, có đáp án chính xác)

`engine/tests/unit/pitch_detect_test.cpp` — đều đạt:
- Nguyên âm a/i/u, nốt giữ từ Mi2 (82 Hz) tới Đô6 (1047 Hz): sai số trung vị ≤ 5 cent, lỗi thô < 1%, 0 lỗi quãng 8.
- Rung giọng 5,5 Hz ±50 cent: trung vị ≤ 8 cent, p95 ≤ 25 cent. Trượt 1 quãng 8 trong 1 giây: ≤ 10 cent.
- 6 thanh điệu tiếng Việt: trung vị ≤ 12 cent, 0 lỗi quãng 8.
- Im lặng / nhiễu trắng / tiếng "s": báo nhầm hữu thanh ≤ 5%.

## Tự chỉnh giọng

### Mô phỏng người hát nghiệp dư (unit test `Corrector.AmateurSingerSimulation`)

Giai điệu ngẫu nhiên Đô trưởng (5 × 60 nốt), mỗi nốt lệch ±20–45 cent, vuốt vào nốt từ −80 cent (60 ms), rung giọng ±35 cent ở nốt dài, có khoảng lặng 40 ms giữa một số nốt.

| | Lệch tâm nốt (trung vị) | Rung giọng giữ lại | Hình dạng thanh điệu (tương quan, thanh tệ nhất) |
|---|---|---|---|
| Gốc (chưa chỉnh) | 31,5 cent | — | — |
| **Tự nhiên** | 1,9 cent | 104% | 0,985 |
| **Ca sĩ** | 0,8 cent | 75% | 0,90 |
| **Robot** | 0,0 cent | 0% | 0,90 |

Lịch sử thiết kế: bản đầu tách "tâm" bằng lọc thông thấp 2 Hz → Tự nhiên chỉ còn 21,5 cent vì bộ lọc trễ và bắt đầu từ đáy đoạn vuốt nốt. Đổi sang **trung bình theo từng nốt** (bỏ 48 ms đầu nốt) và ngưỡng tách nốt tăng theo mức giữ tự nhiên (0,7 + 1,3·p nửa cung) cho kết quả trên.

### Giọng thật (vocadito, key Đô 12 nốt, `pitchlab ab`) — lệch tâm nốt trung vị (cent)

| Bài | Gốc | Tự nhiên | Ca sĩ | Robot |
|---|---|---|---|---|
| vocadito_2 | 8,5 | 10,1 | 6,4 | 2,1 |
| vocadito_5 | 13,2 | 12,2 | 10,5 | 4,3 |
| vocadito_13 | 12,7 | 11,0 | 8,9 | 6,8 |
| vocadito_27 | 13,4 | 11,6 | 7,3 | 4,3 |

Ca sĩ vocadito vốn hát khá chuẩn (lệch ~8–13 cent, nhiều nốt dưới ngưỡng flex 10 cent của Tự nhiên) nên Tự nhiên đổi ít — đúng chủ đích "chỉ đụng chỗ lệch rõ". Lưu ý: chỉ số đo theo lưới A4 = 440 Hz; ca sĩ hát theo tuning khác sẽ bị tính là lệch (dò tuning theo beat làm ở M3).

### PSOLA (unit test `Psola.*`, giọng tổng hợp)
- Dịch ±1/±3/±6 nửa cung với giọng 110/165/330 Hz: f0 ra sai ≤ 5 cent.
- Giữ formant: tương quan đường bao phổ vào/ra ≥ 0,99 (đối chứng đổi tốc độ phát: 0,80).
- Độ trễ đúng 480 mẫu (10 ms); không click; chuyển hữu/vô thanh không vượt biên.

## CPU (benchmark `tonecore_bench`, Mac M1, bản Release)

| Khối | Block 128 | Block 256 |
|---|---|---|
| VoicePitchNode (HPF + dò cao độ + chỉnh + PSOLA, luôn hữu thanh) | 1,2 % một nhân | 1,25 % |
| Engine đầy đủ (mixer + nghe lại + chỉnh giọng) | 1,3 % | 1,3 % |

Chỉ tiêu CLAUDE.md mục 7: chuỗi xử lý giọng ≤ 25% một nhân trên máy tầm trung. Số đo trên iPhone: xem báo cáo M2 (dòng CPU / CPU đỉnh ở màn Phòng thử).

Kiểm tra real-time: test `RealTime.ProcessDoesNotAllocate` đếm `operator new` trong `tc_engine_process_offline` (nhiều cỡ block, đổi preset giữa chừng) = **0**.
