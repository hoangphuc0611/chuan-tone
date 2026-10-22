#!/usr/bin/env python3
"""keylab giả cho test: `fake_keylab.py batch [tham số…] < danh_sach`.

Đọc dự đoán từ file JSON trong biến môi trường FAKE_KEYLAB_PREDICTIONS:
    {"ten_file.mp3": {"key": {"root": 9, "mode": "minor"}, "timeline": [...], "bpm": 68,
                      "duration_sec": 120, "status": "ok"}}
Ghi tham số dòng lệnh (mỗi lần chạy một dòng JSON) vào FAKE_KEYLAB_ARGS_LOG nếu có.
"""

import json
import os
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] != "batch":
        return 2
    log = os.environ.get("FAKE_KEYLAB_ARGS_LOG")
    if log:
        with open(log, "a", encoding="utf-8") as f:
            f.write(json.dumps(sys.argv[2:]) + "\n")
    preds = json.loads(Path(os.environ["FAKE_KEYLAB_PREDICTIONS"]).read_text(encoding="utf-8"))
    for line in sys.stdin:
        file = line.strip()
        if not file:
            continue
        p = preds.get(Path(file).name, {})
        out = {"file": file, "status": p.get("status", "ok"),
               "duration_sec": p.get("duration_sec", 100.0), "sample_rate": 44100, "channels": 2,
               "key": p.get("key"), "timeline": p.get("timeline", []), "bpm": p.get("bpm"),
               "timing_ms": {"total": 1234.0}}
        if out["status"] != "ok":
            out["error"] = "TC_ERR_INVALID_ARG"
        print(json.dumps(out, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
