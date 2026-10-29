# ADR-005: Chuỗi FX giọng và đổi preset không click

- Trạng thái: Đã chấp nhận (2026-10-28 duyệt cùng kế hoạch M5, điểm 1–5; cài đặt 2026-10-29,
  C API 0.6)
- Liên quan: `docs/m5-plan.md` điểm 1–5, CLAUDE.md 3.2 / 3.7, ADR-001 (C API), ADR-002 (độ trễ)

## Bối cảnh

Spec M5: bộ FX đầy đủ, 6 preset thể loại, đổi preset khi đang hát không click (crossfade 10–20 ms).
Delay và reverb có trạng thái kéo dài vài giây (đuôi vang). Crossfade cả chuỗi 15 ms thì cắt cụt
đuôi. Còn trượt thông số trên một chuỗi thì đổi độ dài dòng trễ (thời gian delay, kích thước phòng)
làm cao độ đuôi trượt (Doppler). Audio thread không được cấp phát / khoá.

## Quyết định

1. **Thứ tự chuỗi**: micro → HPF 80 Hz → gate → dò cao độ / chỉnh giọng (`VoicePitchNode`) → EQ →
   compressor → de-esser → delay → reverb → × nghe lại → trộn beat → × master → limiter (nhìn trước
   1 ms) → ra.
   - Gate nằm trong `VoicePitchNode` (trước dò cao độ), mặc định tắt.
   - Phần sau chỉnh giọng là `FxChain`: khô (EQ, nén, de-esser) tách khỏi ướt (delay, reverb).
     Giọng mono tới hết delay, reverb ra stereo.
2. **`FxRack` giữ chuỗi và đổi preset.**
   - **Khác cấu trúc** (bật / tắt khối, loại băng EQ, thời gian delay, kiểu phòng, pre-delay):
     luồng điều khiển dựng `FxChain` mới (cấp phát ở đây), đặt vào ô atomic `pending_` (exchange).
     Chuỗi chưa kịp được nhận thì bị thay và huỷ ngay ở luồng điều khiển → bấm dồn chỉ giữ lần cuối.
   - **Cùng cấu trúc**: gửi bản thông số qua ô atomic `update_` (kèm số thế hệ). Chuỗi đang chạy
     `applyContinuous`, các khối tự trượt (EQ τ 20 ms, độ ướt, hồi tiếp). Bản cũ hơn chuỗi thì bỏ.
   - **Audio thread** nhận chuỗi mới:
     - phần khô crossfade tuyến tính 15 ms (cùng tiếng vào → tương quan cao → cộng tuyến tính không
       hụt);
     - lượng gửi sang delay / reverb cũ giảm 1 → 0 cùng lúc;
     - chuỗi cũ tiếp tục xử lý đuôi với tiếng vào 0 tới khi phần ướt < −80 dBFS, hoặc quá 4 s (khi
       đó tắt dần 15 ms);
     - tối đa 2 chuỗi cùng ngân (quá thì chuỗi ngân lâu nhất tắt dần 15 ms).
   - Chuỗi / bản cập nhật xong được trả về luồng điều khiển qua hàng đợi SPSC để huỷ (giống
     `BeatPlayer`, ADR-004).
3. **C API 0.6**: preset là struct C `tc_fx_preset` (`struct_size`, kẹp giá trị, NaN → lỗi).
   - Hàm: `tc_fx_preset_init(genre)`, `tc_apply_fx_preset`, `tc_get_fx_preset`, `tc_set_tempo`
     (delay theo nhịp).
   - Node `TC_NODE_GATE` (bật, ngưỡng) và `TC_NODE_FX` (bật = Khô / preset; `TC_PARAM_MIX` = "Vang"
     0..2 nhân phần ướt).
   - `tc_metrics.fx_load`, `fx_chains`.
4. **Mặc định**: FX "Khô" và gate tắt → đường giọng như trước M5, đúng từng mẫu (chỉ thêm trễ 1 ms
   của limiter).

## Lý do

- Đuôi vang ngân nốt khi đổi preset giống ca sĩ đổi hiệu ứng trên sân khấu. Test
  `FxRack.StructuralChangeLetsOldTailRingOut`: đổi sang Khô đúng lúc hát xong, đuôi Hall vẫn còn
  200–500 ms sau.
- Ô atomic một phần tử (thay cho hàng đợi) gộp lần bấm dồn tự nhiên, không bao giờ đầy, không phải
  chặn.
- Tách "cấu trúc / liên tục" cho phép thanh trượt (độ ướt, EQ) không dựng lại chuỗi mỗi lần kéo —
  test `FxRack.ContinuousChangeUpdatesInPlace`: 1 chuỗi, đuôi +6 dB đúng khi độ ướt gấp đôi.
- Struct C thay vì JSON: không thêm thư viện vào engine, ffigen sinh kiểu cho Dart.

## Hệ quả

- **CPU khi đổi preset:** delay + reverb tăng gấp đôi trong lúc đuôi cũ ngân (≤ 4 s). Đo trên M1:
  - đổi liên tục, trung bình 1,7 chuỗi: 0,34% một nhân;
  - Bolero đứng yên: 0,24%;
  - Khô: ~0.
- **Bộ nhớ:** mỗi chuỗi ~0,8 MB (dòng trễ 2 s + reverb), tối đa ~7 chuỗi trong ô / hàng đợi.
- **Dọn chuỗi cũ:** chuỗi cũ được huỷ khi app đọc sự kiện (`tc_poll_events`) hoặc đổi preset lần sau.
- **Thông số chưa trượt:** compressor (ngưỡng / tỉ lệ) và de-esser đổi tức thì nhưng mức giảm vẫn
  trượt theo attack / release, nên không click (test `Compressor.ParameterJumpsDoNotClick`).
- **Đo khách quan:** `FxRack.SwapsAreClickFreeWithoutDropouts` đổi qua 7 preset mỗi 200 ms khi đang
  hát. Kết quả:
  - năng lượng trên 12 kHz < −60 dB (crossfade 1 mẫu cho −55 dB → test bắt được);
  - không cửa sổ 10 ms nào tụt quá 3 dB.
