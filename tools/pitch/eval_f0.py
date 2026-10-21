#!/usr/bin/env python3
"""Chấm điểm dò cao độ của ToneCore trên bộ vocadito (CC BY 4.0).

Chạy `pitchlab f0` cho từng file audio, so với nhãn f0 của bộ dữ liệu, in bảng Markdown.

Dùng:
    python3 tools/pitch/eval_f0.py [--data testdata] [--pitchlab engine/build/host/tools/pitchlab]
                                   [--algos mpm,yin] [--out docs/pitch-results.md]

Chỉ số theo định nghĩa của J. Salamon, E. Gómez, D. Ellis, G. Richard, "Melody Extraction from
Polyphonic Music Signals", IEEE Signal Processing Magazine 2014 (giống mir_eval.melody):
  - Voicing Recall (VR): khung có giọng (nhãn) được báo là có giọng.
  - Voicing False Alarm (VFA): khung không có giọng bị báo là có giọng.
  - Raw Pitch Accuracy (RPA): khung có giọng mà cao độ ước lượng lệch ≤ 50 cent. Khi bộ dò báo
    vô thanh vẫn dùng "f0 thô" của khung đó (như mir_eval dùng giá trị âm).
  - Raw Chroma Accuracy (RCA): như RPA nhưng bỏ qua lệch quãng 8.
  - Overall Accuracy (OA): tỉ lệ khung đúng cả hữu/vô thanh lẫn cao độ.
Chỉ dùng thư viện chuẩn Python.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import io
import math
import statistics
import subprocess
import sys
from pathlib import Path


def read_ref(path: Path) -> list[tuple[float, float]]:
    rows = []
    with path.open(newline="") as f:
        for rec in csv.reader(f):
            if len(rec) >= 2:
                rows.append((float(rec[0]), float(rec[1])))
    return rows


def run_pitchlab(pitchlab: Path, audio: Path, algo: str,
                 extra: list[str]) -> list[tuple[float, float, int, float]]:
    out = subprocess.run([str(pitchlab), "f0", str(audio), "--algo", algo, *extra],
                         check=True, capture_output=True, text=True).stdout
    rows = []
    for rec in csv.DictReader(io.StringIO(out)):
        rows.append((float(rec["time"]), float(rec["f0"]), int(rec["voiced"]),
                     float(rec["raw_f0"])))
    return rows


def est_at(est: list[tuple[float, float, int, float]], times: list[float], t: float):
    """Giá trị ước lượng tại thời điểm t: lấy khung gần nhất (hop 4 ms < hop nhãn 5,8 ms)."""
    i = bisect.bisect_left(times, t)
    if i <= 0:
        return est[0]
    if i >= len(times):
        return est[-1]
    return est[i] if times[i] - t < t - times[i - 1] else est[i - 1]


def cents(a: float, b: float) -> float:
    return 1200.0 * math.log2(a / b)


def score(ref, est) -> dict[str, float]:
    times = [e[0] for e in est]
    v_ref = uv_ref = hit_v = fa = rpa = rca = oa = 0
    errs = []
    for t, f_ref in ref:
        _, f_est, voiced, raw = est_at(est, times, t)
        guess = f_est if voiced else raw
        if f_ref > 0:
            v_ref += 1
            if voiced:
                hit_v += 1
            if guess > 0:
                c = cents(guess, f_ref)
                if abs(c) <= 50:
                    rpa += 1
                    if voiced:
                        oa += 1
                if abs(c - 1200 * round(c / 1200)) <= 50:
                    rca += 1
                if voiced:
                    errs.append(abs(c))
        else:
            uv_ref += 1
            if voiced:
                fa += 1
            else:
                oa += 1
    total = v_ref + uv_ref
    return {
        "VR": hit_v / v_ref if v_ref else 1.0,
        "VFA": fa / uv_ref if uv_ref else 0.0,
        "RPA": rpa / v_ref if v_ref else 1.0,
        "RCA": rca / v_ref if v_ref else 1.0,
        "OA": oa / total if total else 1.0,
        "median_cents": statistics.median(errs) if errs else float("nan"),
        "frames": v_ref,
    }


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=root / "testdata")
    ap.add_argument("--pitchlab", type=Path,
                    default=root / "engine/build/host/tools/pitchlab")
    ap.add_argument("--algos", default="mpm,yin")
    ap.add_argument("--out", type=Path, default=None, help="ghi bảng Markdown ra file")
    ap.add_argument("--min-rpa", type=float, default=None,
                    help="thoát với mã 1 nếu RPA trung bình của thuật toán đầu tiên thấp hơn")
    ap.add_argument("extra", nargs="*", help="tham số thêm cho pitchlab f0 (sau --)")
    args = ap.parse_args()

    audio_dir = args.data / "Audio"
    f0_dir = args.data / "Annotations" / "F0"
    if not audio_dir.is_dir():
        print(f"Không thấy {audio_dir}. Chạy tools/pitch/fetch_datasets.sh trước.", file=sys.stderr)
        return 1
    files = sorted(audio_dir.glob("vocadito_*.wav"), key=lambda p: int(p.stem.split("_")[1]))

    lines = [
        "| Thuật toán | VR | VFA | RPA | RCA | OA | Sai số trung vị (cent) |",
        "|---|---|---|---|---|---|---|",
    ]
    per_track: dict[str, list[dict[str, float]]] = {}
    first_rpa = None
    for algo in args.algos.split(","):
        results = []
        for audio in files:
            ref = read_ref(f0_dir / f"{audio.stem}_f0.csv")
            est = run_pitchlab(args.pitchlab, audio, algo, args.extra)
            results.append(score(ref, est))
        per_track[algo] = results
        # Trung bình theo bài (mỗi bài trọng số như nhau, như cách báo cáo của MIREX).
        mean = {k: statistics.fmean(r[k] for r in results)
                for k in ("VR", "VFA", "RPA", "RCA", "OA")}
        med = statistics.median(r["median_cents"] for r in results)
        if first_rpa is None:
            first_rpa = mean["RPA"]
        lines.append(f"| {algo.upper()} | {mean['VR']:.3f} | {mean['VFA']:.3f} | "
                     f"{mean['RPA']:.3f} | {mean['RCA']:.3f} | {mean['OA']:.3f} | {med:.1f} |")

    worst = sorted(range(len(files)), key=lambda i: per_track[args.algos.split(",")[0]][i]["RPA"])
    lines += ["", f"Bài có RPA thấp nhất ({args.algos.split(',')[0].upper()}): " +
              ", ".join(f"{files[i].stem} ({per_track[args.algos.split(',')[0]][i]['RPA']:.2f})"
                        for i in worst[:5])]
    table = "\n".join(lines)
    print(table)
    if args.out:
        args.out.write_text(table + "\n", encoding="utf-8")
    if args.min_rpa is not None and first_rpa is not None and first_rpa < args.min_rpa:
        print(f"RPA {first_rpa:.3f} < {args.min_rpa}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
