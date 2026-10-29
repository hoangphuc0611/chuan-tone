# Công cụ FX giọng (M5)

- `engine/build/host/tools/fxlab` (C++):
  - `render`: cho file giọng qua preset (`--preset bolero`, đúng chuỗi của engine) hoặc từng khối
    (`--gate --hpf --bell --comp --deess --delay --reverb`). In độ to LUFS vào / ra, mức nén, % thời
    gian de-esser làm việc.
  - `response`: bảng đáp ứng EQ so với RBJ.
  - `curve`: đường nén tĩnh.
  - `ir`: RT60 theo băng, độ nhô phổ, tương quan L–R của reverb.
- `fxpage.py build`: trang nghe theo preset `testdata/fx-ab/index.html`.
  - Mỗi preset: giọng vocadito khô so với qua preset, chuẩn hoá −23 LUFS.
  - `fxpage.py score ket-qua.json`: in bảng ý kiến.

Giọng hát nghe thử: vocadito (CC BY 4.0, `tools/pitch/fetch_datasets.sh` tải vào `testdata/`).
