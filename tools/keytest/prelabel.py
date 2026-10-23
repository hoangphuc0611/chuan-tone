#!/usr/bin/env python3
"""Tạo bảng gán nhãn (CSV đúng định dạng keytest) cho một thư mục audio.

    # Tập dev: máy điền sẵn tone / chuyển tone / BPM, người gán chỉ nghe kiểm và sửa
    python3 tools/keytest/prelabel.py testdata/vn-key/dev --out testdata/vn-key/dev.csv

    # Tập test: bảng trống — người gán nghe MÙ (không thấy gợi ý của máy, tránh bị dắt theo)
    python3 tools/keytest/prelabel.py testdata/vn-key/test --out testdata/vn-key/test.csv --blind

Cột thêm (keytest bỏ qua): `may_goi_y` (máy đoán gì, độ tin cậy), `can_nghe_ky` = "x" khi máy
không chắc / có chuyển tone / lỗi, `da_kiem` — người gán ghi "x" sau khi đã nghe xác nhận dòng đó
(validate_csv nhắc các dòng chưa kiểm). Tham số sau `--` chuyển cho `keylab batch`.
"""

from __future__ import annotations

import argparse
import csv
import os
import shlex
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import keytest  # noqa: E402

AUDIO_EXTS = {".mp3", ".m4a", ".aac", ".wav", ".flac", ".ogg"}
HEADER = ["file", "key", "mode", "modulation", "bpm", "genre", "notes",
          "may_goi_y", "can_nghe_ky", "da_kiem"]
KEY_CONFIDENT = 0.5
BPM_CONFIDENT = 0.3
_TONIC_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def find_audio(root: Path) -> list:
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in AUDIO_EXTS)


def mmss(seconds: float) -> str:
    s = int(round(seconds))
    return f"{s // 60}:{s % 60:02d}"


def suggestion_row(rel: str, genre: str, res: dict) -> list:
    """Dòng CSV có gợi ý của máy từ kết quả keylab."""
    if not res or res.get("status") != "ok":
        err = (res or {}).get("error", "không có kết quả")
        return [rel, "", "", "", "", genre, "", f"lỗi: {err}", "x", ""]
    key = keytest.parse_key(res.get("key"))
    conf = float((res.get("key") or {}).get("confidence") or 0.0)
    timeline = keytest.parse_timeline(res)
    mods = [f"{mmss(s.start)}:{_TONIC_NAMES[s.key.tonic]}:{s.key.mode}"
            for s in timeline[1:]]
    bpm = res.get("bpm")
    bpm_conf = float(res.get("bpm_confidence") or 0.0)
    unsure = (key is None or conf < KEY_CONFIDENT or bool(mods)
              or (bpm is not None and bpm_conf < BPM_CONFIDENT))
    first = timeline[0].key if timeline else key
    hint = "không dò được tone" if key is None else f"{key.name} (tin cậy {conf:.2f})"
    if bpm is not None:
        hint += f", {bpm:.0f} BPM (tin cậy {bpm_conf:.2f})"
    return [
        rel,
        _TONIC_NAMES[first.tonic] if first else "",
        first.mode if first else "",
        ";".join(mods),
        f"{bpm:.0f}" if bpm is not None else "",
        genre,
        "",
        hint,
        "x" if unsure else "",
        "",
    ]


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    extra: list = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("audio_dir", type=Path)
    ap.add_argument("--out", type=Path, required=True, help="file CSV ghi ra")
    ap.add_argument("--blind", action="store_true",
                    help="tập test: không điền gợi ý của máy")
    ap.add_argument("--genre", default="", help="điền sẵn cột genre cho mọi bài")
    ap.add_argument("--keylab", help="lệnh chạy keylab (mặc định engine/build/rel|host)")
    ap.add_argument("-j", "--jobs", type=int, default=None)
    ap.add_argument("--force", action="store_true", help="ghi đè file CSV đã có")
    args = ap.parse_args(argv)

    if args.out.exists() and not args.force:
        print(f"{args.out} đã có — thêm --force để ghi đè (nhãn đã gán sẽ mất).",
              file=sys.stderr)
        return 2
    files = find_audio(args.audio_dir)
    if not files:
        print(f"Không thấy file audio trong {args.audio_dir}", file=sys.stderr)
        return 1
    out_dir = args.out.resolve().parent
    rel = {f: Path(os.path.relpath(f.resolve(), out_dir)).as_posix() for f in files}

    rows = []
    if args.blind:
        header = HEADER[:7]
        rows = [[rel[f], "", "", "", "", args.genre, ""] for f in files]
    else:
        header = HEADER
        keylab = args.keylab or keytest.default_keylab()
        if not keylab:
            print("Không thấy keylab — build theo tools/keytest/README.md.", file=sys.stderr)
            return 1
        raw = keytest.run_keylab(shlex.split(keylab), [str(f.resolve()) for f in files],
                                 args.jobs, extra)
        rows = [suggestion_row(rel[f], args.genre, raw.get(str(f.resolve()))) for f in files]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8-sig") as f:  # BOM: Excel đọc đúng UTF-8
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)
    unsure = sum(1 for r in rows if len(r) > 8 and r[8] == "x")
    print(f"{args.out}: {len(rows)} bài" + ("" if args.blind else f", {unsure} bài cần nghe kỹ"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
