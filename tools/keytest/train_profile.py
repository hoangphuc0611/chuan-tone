#!/usr/bin/env python3
"""Học key profile (trưởng / thứ, treble + bass) từ một tập có nhãn, đánh giá bằng kiểm chéo.

    python3 tools/keytest/train_profile.py --set dev --out tools/keytest/profiles/vn-dev.txt
    python3 tools/keytest/train_profile.py --csv testdata/GuitarSet/guitarset.csv --folds 5

Cách học (dựa trên dữ liệu, giống ý tưởng profile EDMA của Faraldo et al. 2016 nhưng học trên
nhạc của mình): với mỗi bài KHÔNG chuyển tone, lấy chroma treble / bass cộng dồn cả bài đúng như
engine dò tone (`keylab --global-chroma on`), xoay về chủ âm = bậc 0, chuẩn hoá tổng = 1, rồi
lấy trung bình theo từng mode → 4 profile (treble trưởng / thứ, bass trưởng / thứ).

Kiểm chéo k-fold: học trên k−1 phần, chấm MIREX phần còn lại bằng `keylab --profile-file`, so với
profile hiện tại của engine trên cùng các bài. Chỉ chạy trên tập dev (không bao giờ trên test).
Tham số sau `--` chuyển cho keylab (vd. `-- --bass-weight 0.5`), dùng cho cả học lẫn chấm.
"""

from __future__ import annotations

import argparse
import datetime
import random
import shlex
import statistics
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dataset  # noqa: E402
import keytest  # noqa: E402
from mirex import MAJOR, MINOR  # noqa: E402

PROFILE_ROWS = ("major", "minor", "bass_major", "bass_minor")


def rotate_to_tonic(vec: list, tonic: int) -> list:
    """Bậc i của kết quả = lớp cao độ (tonic + i)."""
    return [vec[(tonic + i) % 12] for i in range(12)]


def normalized(vec: list) -> list:
    total = sum(vec)
    return [v / total for v in vec] if total > 0 else [0.0] * 12


def learn(samples: list) -> dict:
    """samples: [(Key, bass[12], treble[12])] → {major, minor, bass_major, bass_minor}."""
    out = {}
    for mode in (MAJOR, MINOR):
        rows = [s for s in samples if s[0].mode == mode]
        if not rows:
            raise ValueError(f"không có bài {mode} nào để học")
        for part, idx in (("", 2), ("bass_", 1)):
            vecs = [normalized(rotate_to_tonic(s[idx], s[0].tonic)) for s in rows]
            out[f"{part}{mode}"] = [statistics.fmean(v[i] for v in vecs) for i in range(12)]
    return out


def write_profile(path: Path, profile: dict, header: list) -> None:
    lines = [f"# {h}" for h in header]
    lines += [f"# {name}\n" + " ".join(f"{v:.6f}" for v in profile[name])
              for name in PROFILE_ROWS]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def collect_samples(songs: list, raw: dict) -> list:
    samples = []
    for s in songs:
        res = raw.get(str(s.path)) or {}
        gc = res.get("global_chroma")
        if res.get("status") != "ok" or not gc or s.has_modulation:
            continue  # bài chuyển tone: chroma cả bài trộn nhiều tone — bỏ khi học
        samples.append((s.key, gc["bass"], gc["treble"]))
    return samples


def mean_score(songs: list, raw: dict) -> float:
    return statistics.fmean(keytest.evaluate(s, raw.get(str(s.path))).score for s in songs)


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    extra: list = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--set", choices=("dev", "test"))
    ap.add_argument("--csv")
    ap.add_argument("--keylab")
    ap.add_argument("-j", "--jobs", type=int, default=None)
    ap.add_argument("--folds", type=int, default=5)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", type=Path, help="ghi profile học trên toàn tập")
    args = ap.parse_args(argv)

    csv_path, is_test = keytest.resolve_csv(args)
    if is_test:
        print("Từ chối: chỉ học profile trên tập dev, không bao giờ trên bộ test.",
              file=sys.stderr)
        return 2
    ds = dataset.load(csv_path)
    if ds.errors:
        for e in ds.errors:
            print(f"LỖI  {e}", file=sys.stderr)
        return 1
    keylab = args.keylab or keytest.default_keylab()
    if not keylab:
        print("Không thấy keylab — build theo tools/keytest/README.md.", file=sys.stderr)
        return 1
    cmd = shlex.split(keylab)
    songs = ds.songs
    files = [str(s.path) for s in songs]

    print(f"Đọc chroma {len(songs)} bài…", file=sys.stderr)
    base_raw = keytest.run_keylab(cmd, files, args.jobs, [*extra, "--global-chroma", "on"])
    samples = collect_samples(songs, base_raw)
    print(f"{len(samples)} bài dùng để học (bỏ bài chuyển tone / lỗi).", file=sys.stderr)

    # Kiểm chéo: chia các bài thành k phần cố định theo seed.
    order = list(range(len(songs)))
    random.Random(args.seed).shuffle(order)
    folds = [order[f::args.folds] for f in range(args.folds)]
    learned_scores, base_scores = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for f, held in enumerate(folds):
            held_set = set(held)
            train = [songs[i] for i in order if i not in held_set]
            test = [songs[i] for i in held]
            try:
                profile = learn(collect_samples(train, base_raw))
            except ValueError as e:
                print(f"Phần {f + 1}: {e} — bỏ qua", file=sys.stderr)
                continue
            path = Path(tmp) / f"fold{f}.txt"
            write_profile(path, profile, [f"fold {f + 1}"])
            raw = keytest.run_keylab(cmd, [str(s.path) for s in test], args.jobs,
                                     [*extra, "--profile-file", str(path)])
            learned_scores.append(mean_score(test, raw))
            base_scores.append(mean_score(test, base_raw))
            print(f"Phần {f + 1}/{args.folds}: profile học {learned_scores[-1]:.3f} · "
                  f"hiện tại {base_scores[-1]:.3f} ({len(test)} bài)", file=sys.stderr)

    if learned_scores:
        learned, base = statistics.fmean(learned_scores), statistics.fmean(base_scores)
        print(f"Kiểm chéo {len(learned_scores)} phần: profile học {learned:.3f} so với "
              f"profile hiện tại {base:.3f} ({learned - base:+.3f})")
    if args.out:
        profile = learn(samples)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        write_profile(args.out, profile, [
            f"Học từ {csv_path.name}: {len(samples)} bài, "
            f"{datetime.date.today().isoformat()}, commit {keytest.git_info()}",
            f"keylab: {shlex.join(extra) or '(mặc định)'}",
            *([f"Kiểm chéo {len(learned_scores)} phần: "
               f"{statistics.fmean(learned_scores):.3f} (hiện tại "
               f"{statistics.fmean(base_scores):.3f})"] if learned_scores else []),
        ])
        print(f"Đã ghi {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
