# tools/stretch — đo và nghe thử đổi tone beat (M4)

Công cụ cho `engine/src/stretch/` (`PitchShifter`). Kết quả đo: `docs/stretch-results.md`.

## Build

```sh
tools/stretch/fetch_signalsmith.sh        # tuỳ chọn: mốc so sánh Signalsmith Stretch (MIT) → testdata/ref/
cmake -S engine -B engine/build/rel -DCMAKE_BUILD_TYPE=Release -DTC_BUILD_TESTS=OFF
cmake --build engine/build/rel --target beatlab
```

CMake tự build kèm Signalsmith khi thấy `testdata/ref/signalsmith-stretch/` (đổi chỗ bằng
`-DTC_SIGNALSMITH_DIR=…`). Signalsmith chỉ được gọi như hộp đen qua API công khai trong README để so
A/B — không link vào libtonecore, không ship, không đọc / chép mã bên trong (CLAUDE.md mục 0).

## beatlab

```sh
beatlab eval                                   # bảng chỉ số trên bộ tín hiệu tổng hợp, pv + signalsmith
beatlab eval --algo pv,pv-b2,pv-basic,signalsmith  # thêm B2 (chưa giữ transient) và B1 (cơ bản)
beatlab eval --algo pv --levels -6,6 --hop 512 # chỉ một thuật toán / mức / tham số
beatlab suite out_dir                          # ghi tín hiệu tổng hợp + bản đã đổi tone ra WAV
beatlab render beat.m4a -o out.wav --semitones 3 [--algo signalsmith] [--start 40 --dur 30]
beatlab render beat.m4a -o out.wav --semitones 3 --transients off   # tắt giữ transient để nghe so
```

`render` in một dòng JSON (`duration_sec`, `song_sec`, `render_sec`, `realtime_x`).

## Nghe A/B mù

```sh
# beats.txt: mỗi dòng "đường_dẫn [giây_bắt_đầu]"
python3 tools/stretch/ab.py build --list beats.txt --out testdata/beat-ab --levels -6,-4,-2,2,4,6
open testdata/beat-ab/index.html                # nghe, chấm, bấm "Tải kết quả"
python3 tools/stretch/ab.py score ~/Downloads/ket-qua.json --answers testdata/beat-ab/dap-an.json
```

Trang chỉ có tên file ngẫu nhiên; đáp án ở `dap-an.json`. Beat có bản quyền: chỉ dựng vào
`testdata/` (đã .gitignore), chỉ mở trên máy, không upload.

Test: `python3 -m unittest discover tools/stretch/tests` (dùng beatlab giả, không cần build).
