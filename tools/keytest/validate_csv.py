#!/usr/bin/env python3
"""Kiểm tra file CSV nhãn tone trước khi chấm điểm.

    python3 tools/keytest/validate_csv.py testdata/vn-key/test.csv [--no-check-files]

In lỗi theo từng dòng (thoát mã 1 nếu có lỗi) và thống kê: số bài, trưởng/thứ, số bài chuyển
tone, thể loại, số bài có BPM.
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dataset  # noqa: E402
from mirex import MAJOR  # noqa: E402


def summarize(ds: dataset.Dataset) -> str:
    songs = ds.songs
    major = sum(1 for s in songs if s.key.mode == MAJOR)
    mods = sum(1 for s in songs if s.has_modulation)
    bpm = sum(1 for s in songs if s.bpm is not None)
    genres = collections.Counter(s.genre or "(trống)" for s in songs)
    keys = collections.Counter(s.key.name for s in songs)
    lines = [
        f"{len(songs)} bài hợp lệ: {major} trưởng, {len(songs) - major} thứ; "
        f"{mods} bài chuyển tone; {bpm} bài có BPM.",
        "Thể loại: " + ", ".join(f"{g} {n}" for g, n in genres.most_common()),
        "Tone lúc vào bài: " + ", ".join(f"{k} {n}" for k, n in keys.most_common()),
    ]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv", type=Path)
    ap.add_argument("--no-check-files", action="store_true",
                    help="không kiểm tra file audio có tồn tại")
    args = ap.parse_args(argv)
    ds = dataset.load(args.csv, check_files=not args.no_check_files)
    for e in ds.errors:
        print(f"LỖI  {e}", file=sys.stderr)
    for w in ds.warnings:
        print(f"CHÚ Ý {w}", file=sys.stderr)
    print(summarize(ds))
    if ds.errors:
        print(f"{len(ds.errors)} lỗi.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
