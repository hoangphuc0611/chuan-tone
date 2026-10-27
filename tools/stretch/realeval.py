#!/usr/bin/env python3
"""Đo đổi tone trên beat THẬT (không cần tai): onset ma, tiếng rò trước onset, số mẫu bị cắt.

    python3 tools/stretch/realeval.py --list testdata/beat-ab-list.txt \\
        [--levels -6,-4,-2,2,4,6] [--algos pv,signalsmith] [--dur 15] [--per-beat]

Mỗi beat (`đường_dẫn giây_bắt_đầu`, như tools/stretch/ab.py) được render bằng beatlab ở mỗi mức /
thuật toán (có limiter như engine), rồi so với bản gốc bằng bộ dò onset ĐỘC LẬP với engine (bao
biên năng lượng cao tần 2 ms, bước 2,5 ms, đỉnh trong ±20 ms — khác hẳn tham số của
TransientDetector, để không tự chấm theo ý mình):

- **onset ma**: tiếng đánh có ở bản ra mà bản gốc không có trong ±8 ms — trống "đánh đôi", tiếng
  đánh bị kéo lệch nhịp (nghe A/B 2026-10-27 ứng với nhận xét "nhòe / đánh đôi").
- **rò trước onset**: năng lượng cao tần 5–30 ms trước mỗi onset của bản gốc / 0–20 ms sau, trung vị;
  in chênh với bản gốc (dB, càng gần 0 càng sắc).
- **mẫu bị cắt**: số mẫu chạm ±1 (méo do kẹp).

Kết quả từng bài / mức lưu vào `--cache` (mặc định testdata/realeval/) để chạy lại nhanh.
Beat có bản quyền: chỉ trong testdata/ (đã .gitignore).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ab  # noqa: E402  (parse_list, ROOT, DEFAULT_BEATLAB)


def load_mono(path: Path):
    with wave.open(str(path)) as w:
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels())
    return x.astype(float).mean(axis=1) / 32768.0, rate, int(np.sum(np.abs(x) >= 32767))


def onsets(x, rate, thr=2.0):
    """Onset độc lập: đỉnh của độ tăng log năng lượng cao tần (2 ms), bước 2,5 ms, ±20 ms."""
    h = np.diff(x, prepend=0.0) ** 2
    win = int(0.002 * rate)
    e = np.convolve(h, np.ones(win) / win, "same")
    hop = int(0.0025 * rate)
    lf = np.log(e[::hop] + 1e-10)
    fl = np.maximum(0.0, np.diff(lf, prepend=lf[0]))
    return np.array([i * hop / rate for i in range(12, len(fl) - 12)
                     if fl[i] == fl[i - 8:i + 9].max() and fl[i] > thr])


def pre_echo_db(x, rate, ons):
    h = np.diff(x, prepend=0.0) ** 2
    r = []
    for t in ons:
        i = int(t * rate)
        a = h[max(0, i - int(0.03 * rate)):max(0, i - int(0.005 * rate))].mean()
        b = h[i:i + int(0.02 * rate)].mean() + 1e-20
        r.append(10 * np.log10(a / b + 1e-20))
    return float(np.median(r)) if r else 0.0


def render(beatlab: str, src: Path, out: Path, semis: float, algo: str, start: float, dur: float):
    if out.exists():
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [beatlab, "render", str(src), "-o", str(out), "--semitones", str(semis), "--algo", algo,
           "--start", f"{start:.3f}", "--dur", f"{dur:.3f}"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"beatlab lỗi ({src.name}, {algo} {semis:+g}): {r.stderr.strip()}")


def evaluate(beats, levels, algos, dur, cache: Path, beatlab: str, fresh: bool):
    rows = {}  # (algo, level) → list of per-beat dict
    for src, start in beats:
        start = 0.0 if start is None else start
        tag = f"{src.stem}@{start:.1f}"
        ref_path = cache / f"{tag}_goc.wav"
        if fresh:
            for p in cache.glob(f"{tag}_*.wav"):
                p.unlink()
        render(beatlab, src, ref_path, 0, "pv", start, dur)
        x, rate, _ = load_mono(ref_path)
        on0 = onsets(x, rate)
        pre0 = pre_echo_db(x, rate, on0)
        for algo in algos:
            for n in levels:
                p = cache / f"{tag}_{algo}_{n:+g}.wav"
                render(beatlab, src, p, n, algo, start, dur)
                y, _, clipped = load_mono(p)
                on = onsets(y, rate)
                ghosts = sum(1 for t in on if len(on0) == 0 or np.min(np.abs(on0 - t)) > 0.008)
                rows.setdefault((algo, n), []).append({
                    "beat": src.stem, "ghosts": ghosts, "onsets": len(on0),
                    "pre_db": pre_echo_db(y, rate, on0) - pre0, "clipped": clipped})
    return rows


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    for i in range(len(argv) - 1):  # "--levels -6,-4" → "--levels=-6,-4"
        if argv[i] == "--levels":
            argv[i:i + 2] = [f"--levels={argv[i + 1]}", ""]
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--list", required=True)
    ap.add_argument("--levels", default="-6,-4,-2,2,4,6")
    ap.add_argument("--algos", default="pv,signalsmith")
    ap.add_argument("--dur", type=float, default=15.0)
    ap.add_argument("--cache", default=str(ab.ROOT / "testdata/realeval"))
    ap.add_argument("--beatlab", default=str(ab.DEFAULT_BEATLAB))
    ap.add_argument("--fresh", action="store_true", help="render lại (bỏ bản đã lưu)")
    ap.add_argument("--per-beat", action="store_true")
    ap.add_argument("--json")
    args = ap.parse_args([a for a in argv if a != ""])
    beats = ab.parse_list(Path(args.list).read_text(encoding="utf-8"))
    levels = [float(v) for v in args.levels.split(",")]
    algos = args.algos.split(",")
    rows = evaluate(beats, levels, algos, args.dur, Path(args.cache), args.beatlab, args.fresh)

    print(f"{len(beats)} beat × {args.dur:g} s. Onset ma: tổng các beat (ít hơn = tốt). Rò trước onset: "
          "trung bình chênh với bản gốc (gần 0 = sắc).\n")
    print("| Thuật toán | Mức | Onset ma | Rò trước onset (dB) | Mẫu bị cắt |")
    print("|---|---|---|---|---|")
    for (algo, n), rs in sorted(rows.items()):
        print(f"| {algo} | {n:+g} | {sum(r['ghosts'] for r in rs)} | "
              f"{np.mean([r['pre_db'] for r in rs]):+.1f} | {sum(r['clipped'] for r in rs)} |")
    if args.per_beat:
        print("\n| Beat | Thuật toán | Mức | Onset gốc | Onset ma | Rò (dB) | Cắt |")
        print("|---|---|---|---|---|---|---|")
        for (algo, n), rs in sorted(rows.items()):
            for r in rs:
                print(f"| {r['beat']} | {algo} | {n:+g} | {r['onsets']} | {r['ghosts']} | "
                      f"{r['pre_db']:+.1f} | {r['clipped']} |")
    if args.json:
        Path(args.json).write_text(json.dumps({f"{a}|{n:+g}": rs for (a, n), rs in rows.items()},
                                              ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
