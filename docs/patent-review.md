# Rà soát bằng sáng chế (bản nháp – cần luật sư xem trước khi phát hành)

Tài liệu này sẽ hoàn thiện dần theo milestone. Mỗi mục: số bằng, phạm vi liên quan, cách ToneCore triển khai, khác biệt. **Đây không phải ý kiến pháp lý.**

| Bằng / chủ đề | Liên quan tới | Trạng thái rà soát |
|---|---|---|
| US10930256B2 (chỉnh cao độ real-time trong karaoke trên di động) | Chỉnh giọng M2 | Cần luật sư đọc claims |
| Các bằng về chỉnh cao độ của Antares (vd. US5973252) | Chỉnh giọng M2 | Cần xác minh tình trạng hết hạn |
| Bằng về tìm kiếm bằng fingerprint chòm sao đỉnh phổ (Wang 2003) | Fingerprint bài hát | **Chưa làm** fingerprint (quyết định M8) — rà khi làm kho tone dùng chung |
| Phase vocoder có khoá pha / xử lý transient | Đổi tone beat M4 | Cần tra cứu (mô tả ở mục M4) |
| Dò tone / chuyển tone bằng HMM trên chroma | Dò tone M3 | Cần tra cứu (mô tả ở mục M3) |
| Karaoke tự dò tone → tự đặt chỉnh giọng; đổi tone nhạc → tự đổi key chỉnh giọng | Tổ hợp M3 + M4 + M2 | **Rủi ro chính dạng claim tổ hợp** — so với US10930256B2 và tra cứu thêm |
| PSOLA (France Télécom/CNET) | Pitch shifter M2 | Cần xác minh tình trạng hết hạn |

## M2 — Dò cao độ & tự chỉnh giọng (mô tả để luật sư so với claims)

Mã nguồn: `engine/src/pitch/`. Viết từ tài liệu công khai (xem `docs/references.md`), không tham khảo mã hay sản phẩm của bên khác.

1. **Dò cao độ**: McLeod Pitch Method (bài báo ICMC 2005) trên tín hiệu hạ mẫu ~16 kHz, cửa sổ 40 ms, hop 4 ms; theo dõi f0 bằng ngưỡng clarity hai mức, chặn bước nhảy > 700 cent bằng xác nhận 3 khung, lọc trung vị 3 khung.
2. **Chọn nốt đích**: người dùng chọn tone bằng tay (chủ âm + thang âm); cao độ được lượng tử về nốt gần nhất thuộc thang, có vùng trễ (hysteresis) ở ranh giới hai nốt.
3. **Đường cong chỉnh**: chia giọng thành nốt (theo khoảng cách tới trung bình nốt), tâm nốt = trung bình trượt ≤ 400 ms bỏ 48 ms đầu nốt; lượng chỉnh = nốt đích − (p·tâm nốt + (1−p)·cao độ tức thời), làm mượt bậc 2 theo `retune`, chậm dần trên nốt dài (`humanize`), bỏ qua lệch nhỏ (`flex`), kẹp ≤ 200 cent.
4. **Dịch cao độ**: TD-PSOLA (Moulines & Charpentier 1990) với độ trễ cố định 10 ms; mốc chu kỳ đặt theo f0 rồi tinh chỉnh bằng tương quan chuẩn hoá với chu kỳ trước; chồng hạt Hann có chuẩn hoá theo tổng cửa sổ; đoạn vô thanh đi thẳng.
5. **Ứng dụng karaoke trên di động**: chạy real-time trong audio callback, nghe lại qua tai nghe; tone chọn tay ở M2 (tự dò tone ở M3).

Điểm cần luật sư xác minh:
- **US10930256B2**: so các claim độc lập với mục 1–5 ở trên (đặc biệt cách xác định nốt đích và cách áp dịch cao độ trong luồng karaoke).
- **US5973252 (Antares)**: nộp đơn năm 1998 → theo thời hạn 20 năm có thể đã hết hiệu lực; cần xác minh trạng thái chính thức.
- **PSOLA (France Télécom/CNET, cuối thập niên 1980)**: có thể đã hết hạn; cần xác minh số bằng và trạng thái.

Tên tính năng trong app là "Tự chỉnh giọng" (không dùng "Auto-Tune").

## M3–M8 — mô tả để luật sư so với claims (M9 B6, 2026-11-20)

Mọi phần viết từ tài liệu công khai (`docs/references.md`), không tham khảo mã hay sản phẩm của bên
khác.

### M3 — Dò tone, chuyển tone, BPM, loudness (`engine/src/key/`, ADR-003)

1. Phân tích **offline** cả file beat (không real-time).
2. Tách thành phần trống:
   - HPSS lọc trung vị (Fitzgerald 2010);
   - CQT theo quãng 8 (Schörkhuber & Klapuri 2010) → chroma 12 bin đã bù tuning.
3. Tone cả bài: tương quan chroma với profile tone (Krumhansl–Kessler / Temperley / EDMA), chọn
   profile trên tập dev.
4. Chuyển tone: HMM 24 trạng thái trên các đoạn vài giây, giải bằng Viterbi (Noland & Sandler 2006;
   Rabiner 1989). Đầu ra là timeline `[bắt đầu, kết thúc, tone, độ tin cậy]`. App gộp đoạn đầu /
   cuối ngắn hơn 20 s vào đoạn kề (M9 B2).
5. BPM: tự tương quan onset (Percival & Tzanetakis 2014).
6. Loudness: ITU-R BS.1770-4.

### M4 — Đổi tone beat ±6 nửa cung (`engine/src/stretch/`, ADR-004)

1. Phase vocoder FFT 4096 (Flanagan & Golden 1966; Dolson 1986), khoá pha identity (Laroche &
   Dolson 1999).
2. Giữ transient: dò onset bằng spectral flux; tại khung có onset, bin trội lấy pha tuyến tính
   (tự thiết kế, ý tưởng từ Duxbury 2002 / Röbel 2003).
3. Đổi cao độ = giãn thời gian + resample sinc–Kaiser.
4. **Render ở luồng nền** từ vị trí đang phát, theo khối 85 ms. Luồng âm thanh chỉ đọc PCM đã
   render + crossfade 20 ms. Giới hạn đỉnh nhìn trước.
5. Đổi tone nhạc thì engine tự cộng số nửa cung vào key chỉnh giọng (ADR-004 điểm 5).

### M5 — Hiệu ứng (`engine/src/fx/`, ADR-005)

1. Kỹ thuật kinh điển:
   - gate;
   - EQ SVF (Zavalishin);
   - compressor knee mềm (Giannoulis 2012);
   - de-esser shelf động;
   - delay theo nhịp;
   - reverb FDN 8 dòng (Jot & Chaigne 1991);
   - limiter nhìn trước.
2. Đổi preset khi đang hát: hai chuỗi FX song song. Phần khô crossfade 15 ms; đuôi vang của chuỗi
   cũ ngân tới −80 dB.
3. Preset theo thể loại; gợi ý thể loại từ tên bài + BPM.

### M6 — Tách giọng ca sĩ (`server/`, ADR-006)

1. Dùng model htdemucs của bên thứ ba qua API công khai. **Trọng số chỉ cho phát triển nội bộ**
   (`docs/licenses.md`).
2. Máy người dùng nén FLAC (bộ nén tự viết theo RFC 9639), chia đoạn, gửi khi người dùng đồng ý.
   Máy chủ chỉ trả stem giọng; máy tính phần nhạc = bài − giọng.

### M7 — Thu âm, bù độ trễ, xuất video (`engine/src/record/`, ADR-007)

1. Ghi giọng khô / giọng đã xử lý / beat.
2. Bù độ trễ đo bằng loopback (phát xung → thu lại), áp lúc đọc.
3. Căn hình – tiếng theo đồng hồ máy.
4. Xuất chuẩn hoá −14 LUFS, true-peak ≤ −1 dBTP.
5. Video nền sóng nhạc vẽ bằng CoreGraphics từ phổ của chính file tiếng.

### Tổ hợp trong app (điểm cần luật sư xem kỹ nhất)

Luồng chính:
1. Người dùng chọn bài.
2. App **tự dò tone offline** (M3).
3. Chỉnh giọng real-time **tự theo timeline tone** của bài (M2 + M3).
4. Đổi tone nhạc thì key chỉnh giọng **tự đổi theo** (M4).
5. Hiệu ứng **tự chọn theo thể loại** (M5).
6. Thu âm có bù độ trễ đo được (M7).

Từng kỹ thuật là kỹ thuật công khai. Nhưng các bằng về karaoke trên di động có thể có claim dạng
tổ hợp. Cần:
- đối chiếu claim độc lập của US10930256B2 với các bước 1–6;
- tra thêm bằng về "tự xác định key của bài để đặt tham số chỉnh cao độ" và "đổi key nhạc đệm kéo
  theo đổi key chỉnh cao độ".

