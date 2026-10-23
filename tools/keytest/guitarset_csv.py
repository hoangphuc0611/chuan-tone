#!/usr/bin/env python3
"""Tạo CSV nhãn keytest từ GuitarSet (CC BY 4.0): python3 guitarset_csv.py testdata/GuitarSet

Đọc annotation/*.jams (namespace key_mode, tempo), ghi guitarset.csv cạnh thư mục audio_mono-mic/.
Thể loại = phong cách trong tên file (BN bossa nova, Funk, Jazz, Rock, SS singer-songwriter) +
"-comp" (đệm hợp âm) hoặc "-solo" (bè chính).
"""

from __future__ import annotations

import csv
import json
import re
import sys
from pathlib import Path

NAME = re.compile(r"^(\d+)_([A-Za-z]+)\d+-(\d+)-([A-G][b#]?)_(comp|solo)$")


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    root = Path(argv[0])
    rows, missing = [], 0
    for jams in sorted((root / "annotation").glob("*.jams")):
        m = NAME.match(jams.stem)
        if not m:
            print(f"Bỏ qua tên lạ: {jams.name}", file=sys.stderr)
            continue
        _, style, _, _, part = m.groups()
        data = json.loads(jams.read_text(encoding="utf-8"))
        key = tempo = None
        for ann in data["annotations"]:
            if ann["namespace"] == "key_mode" and ann["data"]:
                key = ann["data"][0]["value"]
            elif ann["namespace"] == "tempo" and ann["data"]:
                tempo = ann["data"][0]["value"]
        if key is None or ":" not in key:
            print(f"Thiếu key_mode: {jams.name}", file=sys.stderr)
            continue
        tonic, mode = key.split(":", 1)
        audio = Path("audio_mono-mic") / f"{jams.stem}_mic.wav"
        if not (root / audio).is_file():
            missing += 1
        rows.append([audio.as_posix(), tonic, mode, "", f"{float(tempo):g}" if tempo else "",
                     f"{style}-{part}", ""])
    out = root / "guitarset.csv"
    with out.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "key", "mode", "modulation", "bpm", "genre", "notes"])
        w.writerows(rows)
    print(f"{out}: {len(rows)} bài" + (f" (thiếu {missing} file audio)" if missing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
