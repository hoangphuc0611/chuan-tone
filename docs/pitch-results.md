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

## Chỉnh giọng, CPU

(Cập nhật ở các bước sau của M2.)
