# keytest — chấm điểm dò tone / BPM

Chấm ToneCore trên bộ bài Việt có nhãn theo cách tính điểm MIREX (CLAUDE.md mục 3.10). Chỉ tiêu
M3: **điểm trung bình ≥ 0,85 trên bộ test 50 bài**.

## 1. Chuẩn bị dữ liệu

```
testdata/vn-key/          ← đã .gitignore: KHÔNG commit audio có bản quyền
├── dev.csv               ← ~150 bài để chỉnh thuật toán
├── test.csv              ← 50 bài chỉ để chấm điểm cuối
├── dev/…mp3|m4a|wav
└── test/…
```

Hai tập phải **khác bài hoàn toàn**. Hai bản beat của cùng một bài cũng không được nằm ở hai
tập khác nhau. Xem file mẫu `tools/keytest/example.csv`.

### Định dạng CSV

UTF-8. Lưu từ Excel hay Google Sheets đều được, dấu phân cách `,` `;` hoặc tab.

| Cột | Bắt buộc | Ví dụ | Ghi chú |
|---|---|---|---|
| `file` | có | `dev/duyen_phan.mp3` | đường dẫn tương đối so với thư mục chứa CSV |
| `key` | có | `A`, `C#`, `Eb` (hoặc gọn: `Am`) | chủ âm, tên quốc tế, `#` thăng, `b` giáng |
| `mode` | có (trừ khi `key` viết gọn) | `minor` / `major` (hoặc `thứ` / `trưởng`) | |
| `modulation` | không | `1:35:A:major;2:40:Am` | các mốc chuyển tone, xem bên dưới |
| `bpm` | không | `68` | để chấm dò BPM |
| `genre` | không | `bolero` | `bolero`, `ballad`, `nhac-tre`, `remix`, `rap`, `lofi`, `dan-ca`, `khac` |
| `notes` | không | `phân vân Am / C` | ghi chú tự do |

Cột khác (vd. `artist`) được bỏ qua.

### Quy ước gán nhãn (gửi cho người gán)

1. **Tone theo đúng file beat**, không theo bản gốc của ca sĩ. Beat "tone nữ" đã dịch so với
   bản gốc thì ghi tone của beat.
2. **`key` + `mode` là tone lúc vào bài**, tức câu hát đầu tiên sau intro. Thường intro cùng tone
   với bài. Nếu intro ở tone khác thì ghi vào `notes`.
3. **Chuyển tone**: mỗi mốc ghi `thời điểm:chủ âm:mode`, các mốc cách nhau bằng `;`.
   - Thời điểm là lúc **bắt đầu câu nhạc đầu tiên ở tone mới**. Viết theo giây (`95.5`) hoặc
     phút:giây (`1:35`). Lệch ±2 giây vẫn được.
   - Chỉ ghi khi tone mới kéo dài **ít nhất một câu nhạc (~8 giây)**. Hợp âm mượn ngắn hay
     một câu chuyển tiếp thì không ghi.
   - Ví dụ bolero vào Am, điệp khúc lên A trưởng lúc 1:35, rồi quay về Am lúc 2:40:
     `1:35:A:major;2:40:A:minor`. Viết gọn cũng được: `1:35:A;2:40:Am`.
   - Nâng tone cuối bài (vd. lên nửa cung lúc 3:10) cũng ghi: `3:10:Bb:minor`.
4. **Trưởng hay thứ** khi bài ngũ cung hoặc dân ca: chọn theo **chủ âm**, tức nốt hay hợp âm bài
   kết về và nghe "yên" nhất. Phân vân thì vẫn chọn một, rồi ghi vào `notes`
   (vd. `phân vân Am / C`).
5. **BPM**: số phách mỗi phút theo cách nhạc công đếm, tức nốt đen theo ký âm. Bolero thường đếm
   khoảng 60–75. Bỏ trống nếu không chắc.
6. Ghi tên tone kiểu nào cũng được (`C#` hay `Db`), keytest tự hiểu.

### Nhãn lấy từ tiêu đề video karaoke

Nhiều kênh karaoke (vd. Trọng Hiếu, Nhan KTV) ghi tone của beat ngay trong tiêu đề: "Tone Nam ( Cm )",
"Tone Nữ ( Am )". Đó là tone do người làm beat ghi — độc lập với thuật toán của ToneCore — nên dùng
làm đáp án được. Các dòng như vậy có `nguon_nhan = tieu-de` và cột `tieu_de`, `url` để đối chiếu.

- Chỉ là **tone chính** (không có mốc chuyển tone) → bài này được chấm theo tone toàn bài.
- Có thể sai (kênh gõ nhầm, ghi tone bản gốc thay vì beat) → người biết nhạc nghe kiểm ngẫu nhiên
  10–20 bài của bộ test; sai nhiều thì phải gán lại.
- Bài **không** có tone trong tiêu đề: chỉ có gợi ý của máy → để riêng (`dev-pending.csv`), người
  nghe xác nhận xong mới đưa vào `dev.csv`. Không bao giờ chấm điểm bằng nhãn do chính máy gợi ý.

### Gán nhãn nhanh hơn với gợi ý của máy

```sh
# Tập dev: máy điền sẵn tone / chuyển tone / BPM; cột can_nghe_ky = "x" ở bài máy không chắc.
python3 tools/keytest/prelabel.py testdata/vn-key/dev --out testdata/vn-key/dev.csv --genre bolero
# Tập test: bảng TRỐNG — nghe mù, không xem gợi ý của máy (tránh bị dắt theo máy).
python3 tools/keytest/prelabel.py testdata/vn-key/test --out testdata/vn-key/test.csv --blind
```

Mở file bằng Excel / Google Sheets, nghe từng bài, sửa ô sai rồi ghi `x` vào cột `da_kiem` của
dòng đã nghe xong — `validate_csv.py` nhắc các dòng chưa kiểm. Không ghi đè file đã có (trừ khi
thêm `--force`).

Kiểm tra file trước khi gửi:

```sh
python3 tools/keytest/validate_csv.py testdata/vn-key/dev.csv
```

Lệnh này báo lỗi theo từng dòng (tên tone sai, mốc thời gian không tăng dần, thiếu file,
trùng bài…) và in thống kê số bài trưởng/thứ, chuyển tone, thể loại.

## 2. Chạy chấm điểm

```sh
# Build keylab (Release cho nhanh)
cmake -S engine -B engine/build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DTC_BUILD_TESTS=OFF
cmake --build engine/build/rel --target keylab

# Chấm trên dev, lưu báo cáo
python3 tools/keytest/keytest.py run --set dev --out dev-report.md --per-song dev.csv

# Quét tham số (CHỈ trên dev)
python3 tools/keytest/keytest.py sweep --set dev --grid profile=kk,temperley-kp --grid bass-weight=0,0.25,0.5

# Chấm bộ test (chỉ với tham số mặc định)
python3 tools/keytest/keytest.py run --set test --out docs/key-results.md
```

Thử một bài: `engine/build/rel/tools/keylab analyze bai.mp3` (in JSON).

Bộ công khai để thử khi chưa có dữ liệu Việt: `tools/keytest/fetch_guitarset.sh` tải GuitarSet
(CC BY 4.0, ~700 MB) và tạo `testdata/GuitarSet/guitarset.csv`; kết quả ở `docs/key-results.md`.
Tham số keylab quét được (xem `keylab` không tham số): tone `--profile kk|temperley|temperley-kp|diatonic`,
`--bass-weight`, `--frame-norm none|l1|max`, `--silence-db`, `--compression`; timeline
`--timeline on|off`, `--switch-prob`, `--emission-temp`, `--min-segment`, `--window-sec`,
`--related-boost`; BPM `--tempo-prior`, `--tempo-octaves`; profile từ file `--profile-file`;
gợi ý chủ âm cuối bài `--end-weight`, `--end-sec`.

### Cách tính điểm

| Tone đoán so với đáp án | Điểm |
|---|---|
| Đúng | 1,0 |
| Cùng mode, cao hơn một quãng 5 (C → G) | 0,5 |
| Tương đối trưởng/thứ (C ↔ Am) | 0,3 |
| Song song (Am ↔ A) | 0,2 |
| Khác | 0 |

Đây là bảng chuẩn MIREX, giống `mir_eval.key.weighted_score`. Báo cáo in thêm điểm khi tính lệch
quãng 5 cả hai chiều để tham khảo.

- Bài **không** chuyển tone: so tone toàn bài.
- Bài **có** chuyển tone: lấy trung bình điểm theo thời lượng trên timeline.
- Báo cáo in thêm: điểm chỉ so tone chính; số bài không chuyển tone mà bị đoán có chuyển tone;
  BPM Acc1 (đúng ±4%) và Acc2 (chấp nhận ×2, ×3, ×½, ×⅓); thời gian phân tích.

### Học profile từ dữ liệu

```sh
# Kiểm chéo 5 phần trên tập dev rồi ghi profile học được
python3 tools/keytest/train_profile.py --set dev --folds 5 --out tools/keytest/profiles/vn-dev.txt
# Chấm thử với profile đó
python3 tools/keytest/keytest.py run --set dev -- --profile-file tools/keytest/profiles/vn-dev.txt
```

Học từ các bài không chuyển tone: chroma cả bài xoay về chủ âm rồi lấy trung bình theo trưởng /
thứ, riêng cho treble và bass. Chỉ chạy trên dev. Nếu thắng profile có sẵn trên dev thì đưa vào
`engine/src/key/key_profiles.cpp` làm profile mặc định.

### Chống "học vẹt" bộ test

- Mọi việc chỉnh thuật toán làm trên `dev`.
- Với bộ test, `run` từ chối khi có tham số keylab tuỳ chỉnh (sau `--`), và `sweep` bị chặn hẳn.
- Mỗi lần chạy bộ test được ghi vào `testdata/vn-key/test-runs.jsonl`, báo cáo in số thứ tự lần
  chạy.

### Kiểm chéo với mir_eval (không bắt buộc)

```sh
/usr/bin/python3 -m venv .venv && .venv/bin/pip install mir_eval==0.8.2
.venv/bin/python tools/keytest/keytest.py run --set dev     # báo cáo có dòng "Kiểm chéo mir_eval"
```

Test của công cụ:
`KEYLAB=engine/build/rel/tools/keylab .venv/bin/python -m unittest discover -v tools/keytest/tests`
