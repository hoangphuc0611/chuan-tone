#!/usr/bin/env python3
"""Chấm điểm dò tone / BPM của ToneCore trên bộ dữ liệu có nhãn (MIREX Audio Key Detection).

    # Chạy và chấm (mặc định đọc testdata/vn-key/{dev,test}.csv)
    python3 tools/keytest/keytest.py run --set dev [--out bao_cao.md] [--per-song bai.csv]
    python3 tools/keytest/keytest.py run --csv duong/dan/bo.csv -- --profile temperley

    # Quét tham số (CHỈ trên tập dev)
    python3 tools/keytest/keytest.py sweep --set dev --grid profile=kk,temperley --grid bass=0.5,1

Tham số sau `--` được chuyển thẳng cho `keylab batch`. Cách tính điểm: xem mirex.py; định dạng
CSV: dataset.py / README.md. Nếu cài `mir_eval` (MIT) thì tự kiểm chéo điểm từng bài.

Chống "học vẹt" bộ test (docs/m3-plan.md): với bộ test, `run` chỉ chạy với tham số mặc định của
keylab, `sweep` bị từ chối, và mỗi lần chạy được ghi vào test-runs.jsonl cạnh file CSV.
"""

from __future__ import annotations

import argparse
import csv
import datetime
import itertools
import json
import shlex
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dataset  # noqa: E402
import mirex  # noqa: E402
from mirex import Key, Segment  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = ROOT / "testdata" / "vn-key"
KEYLAB_CANDIDATES = [ROOT / "engine/build/rel/tools/keylab", ROOT / "engine/build/host/tools/keylab"]
BPM_TOLERANCE = 0.04
BPM_FACTORS = (1.0, 2.0, 3.0, 1.0 / 2.0, 1.0 / 3.0)
TARGET = 0.85


# ---------------------------------------------------------------------------------------------
# Chạy keylab
# ---------------------------------------------------------------------------------------------

def default_keylab() -> Optional[str]:
    for p in KEYLAB_CANDIDATES:
        if p.is_file():
            return str(p)
    return None


def run_keylab(cmd: list, files: list, jobs: Optional[int], extra: list) -> dict:
    """Chạy `keylab batch`, trả {đường dẫn: kết quả JSON}."""
    args = [*cmd, "batch", *(["-j", str(jobs)] if jobs else []), *extra]
    results = {}
    tty = sys.stderr.isatty()
    with subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                          encoding="utf-8") as proc:
        assert proc.stdin is not None and proc.stdout is not None
        proc.stdin.write("".join(f"{f}\n" for f in files))
        proc.stdin.close()
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            res = json.loads(line)
            results[res["file"]] = res
            if tty:
                print(f"\r  [{len(results)}/{len(files)}] {Path(res['file']).name[:60]:<60}",
                      end="", file=sys.stderr, flush=True)
        code = proc.wait()
    if tty:
        print(file=sys.stderr)
    if code != 0:
        raise RuntimeError(f"keylab thoát với mã {code}: {shlex.join(args)}")
    return results


def parse_key(obj) -> Optional[Key]:
    if not obj:
        return None
    return Key(int(obj["root"]) % 12, mirex.parse_mode(obj["mode"]))


def parse_timeline(res: dict) -> list:
    segs = []
    for s in res.get("timeline") or []:
        segs.append(Segment(float(s["start"]), float(s["end"]), parse_key(s)))
    return sorted(segs, key=lambda s: s.start)


# ---------------------------------------------------------------------------------------------
# Chấm điểm
# ---------------------------------------------------------------------------------------------

@dataclass
class SongResult:
    song: dataset.Song
    ok: bool
    error: str
    duration: float
    ref_main: Key
    est_key: Optional[Key]
    est_timeline: list
    score: float           # điểm tính chỉ tiêu
    score_main: float      # chỉ so tone chính
    score_desc5: float     # như `score` nhưng tính quãng 5 cả hai chiều
    relation: str          # quan hệ tone chính đoán ↔ đáp án
    spurious: bool         # bài không chuyển tone nhưng đoán ra nhiều tone
    est_bpm: Optional[float]
    acc1: Optional[bool]
    acc2: Optional[bool]
    time_ms: Optional[float]


def bpm_accuracy(ref: float, est: Optional[float]) -> tuple:
    if est is None or est <= 0:
        return False, False
    acc1 = abs(est - ref) <= BPM_TOLERANCE * ref
    acc2 = any(abs(est - f * ref) <= BPM_TOLERANCE * f * ref for f in BPM_FACTORS)
    return acc1, acc2


def evaluate(song: dataset.Song, res: Optional[dict]) -> SongResult:
    ok = bool(res) and res.get("status") == "ok"
    duration = float(res.get("duration_sec") or 0.0) if ok else 0.0
    ref_tl = song.reference_timeline(duration) if duration > 0 else []
    ref_main = mirex.main_key(ref_tl) or song.key
    est_key = parse_key(res.get("key")) if ok else None
    est_tl = parse_timeline(res) if ok else []
    # Chưa có timeline nhưng có tone toàn bài → coi như một đoạn suốt bài.
    est_tl_scored = est_tl or ([Segment(0.0, duration, est_key)] if est_key else [])

    def primary(desc5: bool) -> float:
        if not ok or not ref_tl:
            return 0.0
        if song.has_modulation:
            return mirex.timeline_score(ref_tl, est_tl_scored, desc5)
        return mirex.score(song.key, est_key, desc5)

    est_bpm = res.get("bpm") if ok else None
    acc1 = acc2 = None
    if song.bpm is not None:
        acc1, acc2 = bpm_accuracy(song.bpm, est_bpm)
    timing = (res or {}).get("timing_ms") or {}
    return SongResult(
        song=song, ok=ok, error="" if ok else (res or {}).get("error", "không có kết quả"),
        duration=duration, ref_main=ref_main, est_key=est_key, est_timeline=est_tl,
        score=primary(False), score_main=mirex.score(ref_main, est_key) if ok else 0.0,
        score_desc5=primary(True), relation=mirex.relation(ref_main, est_key),
        spurious=not song.has_modulation and len({s.key for s in est_tl}) > 1,
        est_bpm=est_bpm, acc1=acc1, acc2=acc2, time_ms=timing.get("total"))


def mir_eval_check(results: list) -> str:
    """So điểm tone chính với mir_eval.key.weighted_score (nếu đã cài)."""
    try:
        import mir_eval.key  # type: ignore
    except ImportError:
        return "bỏ qua (chưa cài mir_eval)"
    n = bad = 0
    for r in results:
        if r.est_key is None:
            continue
        n += 1
        theirs = mir_eval.key.weighted_score(r.ref_main.mir_eval_name, r.est_key.mir_eval_name)
        if abs(theirs - r.score_main) > 1e-9:
            bad += 1
    return f"khớp {n - bad}/{n}" + (f" — **{bad} bài lệch, kiểm tra mirex.py**" if bad else "")


@dataclass
class Summary:
    n: int
    errors: int
    score: float
    score_main: float
    score_desc5: float
    mod_n: int
    mod_score: Optional[float]
    spurious: int
    nomod_n: int
    bpm_n: int
    acc1: Optional[float]
    acc2: Optional[float]
    time_median_ms: Optional[float]
    time_max_ms: Optional[float]


def summarize(results: list) -> Summary:
    def mean(xs):
        xs = list(xs)
        return statistics.fmean(xs) if xs else None

    mods = [r for r in results if r.song.has_modulation]
    bpm = [r for r in results if r.acc1 is not None]
    times = [r.time_ms for r in results if r.time_ms is not None and r.ok]
    return Summary(
        n=len(results), errors=sum(1 for r in results if not r.ok),
        score=mean(r.score for r in results) or 0.0,
        score_main=mean(r.score_main for r in results) or 0.0,
        score_desc5=mean(r.score_desc5 for r in results) or 0.0,
        mod_n=len(mods), mod_score=mean(r.score for r in mods),
        spurious=sum(1 for r in results if r.spurious), nomod_n=len(results) - len(mods),
        bpm_n=len(bpm), acc1=mean(1.0 if r.acc1 else 0.0 for r in bpm),
        acc2=mean(1.0 if r.acc2 else 0.0 for r in bpm),
        time_median_ms=statistics.median(times) if times else None,
        time_max_ms=max(times) if times else None)


# ---------------------------------------------------------------------------------------------
# Báo cáo
# ---------------------------------------------------------------------------------------------

def fmt(v: Optional[float], spec: str = ".3f") -> str:
    return "—" if v is None else format(v, spec)


def pct(v: Optional[float]) -> str:
    return "—" if v is None else f"{100 * v:.0f}%"


def secs(ms: Optional[float]) -> str:
    return "—" if ms is None else f"{ms / 1000:.2f} s"


def est_label(r: SongResult) -> str:
    if not r.ok:
        return f"lỗi: {r.error}"
    if r.est_key is None:
        return "—"
    keys = []
    for s in r.est_timeline:
        if not keys or keys[-1] != s.key.name:
            keys.append(s.key.name)
    label = r.est_key.name
    if len(keys) > 1:
        label += f" ({' → '.join(keys)})"
    return label


def ref_label(song: dataset.Song) -> str:
    label = song.key.name
    if song.modulations:
        label += " → " + " → ".join(f"{k.name}@{t:g}s" for t, k in song.modulations)
    return label


def git_info() -> str:
    try:
        rev = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"],
                               capture_output=True, text=True, check=True).stdout.strip()
        return rev + (" (+ thay đổi chưa commit)" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "?"


def render_report(title: str, results: list, s: Summary, meta: list) -> str:
    lines = [f"# {title}", ""]
    lines += [f"- {m}" for m in meta]
    lines += [
        "",
        "| Chỉ số | Giá trị |",
        "|---|---|",
        f"| **Điểm MIREX** (chỉ tiêu ≥ {TARGET}) | **{s.score:.3f}** |",
        f"| Chỉ tone chính | {s.score_main:.3f} |",
        f"| Tính lệch quãng 5 cả hai chiều | {s.score_desc5:.3f} |",
        f"| Bài chuyển tone: số bài / điểm timeline | {s.mod_n} / {fmt(s.mod_score)} |",
        f"| Chuyển tone giả (bài không chuyển tone) | {s.spurious}/{s.nomod_n} |",
        f"| BPM Acc1 / Acc2 (số bài có nhãn) | {pct(s.acc1)} / {pct(s.acc2)} ({s.bpm_n}) |",
        f"| Thời gian phân tích: trung vị / lớn nhất | {secs(s.time_median_ms)} / "
        f"{secs(s.time_max_ms)} |",
        f"| Bài lỗi | {s.errors}/{s.n} |",
        f"| Kiểm chéo mir_eval | {mir_eval_check(results)} |",
        "",
        "## Phân loại lỗi (tone chính)",
        "",
        "| Loại | Số bài | Tỉ lệ |",
        "|---|---|---|",
    ]
    for rel, label in mirex.RELATION_LABELS.items():
        n = sum(1 for r in results if r.relation == rel)
        if n:
            lines.append(f"| {label} | {n} | {pct(n / len(results))} |")
    genres: dict = {}
    for r in results:
        genres.setdefault(r.song.genre or "(trống)", []).append(r)
    if len(genres) > 1:
        lines += ["", "## Theo thể loại", "", "| Thể loại | Số bài | Điểm MIREX | Đúng hẳn |",
                  "|---|---|---|---|"]
        for g in sorted(genres):
            rs = genres[g]
            right = sum(1 for r in rs if r.relation == mirex.CORRECT)
            lines.append(f"| {g} | {len(rs)} | {statistics.fmean(r.score for r in rs):.3f} | "
                         f"{pct(right / len(rs))} |")
    lines += [
        "",
        "## Từng bài (điểm thấp trước)",
        "",
        "| Bài | Nhãn | Đoán | Điểm | Loại | BPM nhãn → đoán | Thời gian |",
        "|---|---|---|---|---|---|---|",
    ]
    for r in sorted(results, key=lambda r: (r.score, r.song.file)):
        bpm = "—"
        if r.song.bpm is not None:
            bpm = f"{r.song.bpm:g} → {fmt(r.est_bpm, '.1f')}" + (" ✓" if r.acc1 else "")
        lines.append(f"| {r.song.file} | {ref_label(r.song)} | {est_label(r)} | {r.score:.2f} | "
                     f"{mirex.RELATION_LABELS[r.relation]} | {bpm} | {secs(r.time_ms)} |")
    return "\n".join(lines) + "\n"


def write_per_song(path: Path, results: list) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "ref", "est", "score", "score_main", "relation", "spurious",
                    "ref_bpm", "est_bpm", "acc1", "acc2", "time_ms", "error"])
        for r in results:
            w.writerow([r.song.file, ref_label(r.song), r.est_key.name if r.est_key else "",
                        f"{r.score:.4f}", f"{r.score_main:.4f}", r.relation, int(r.spurious),
                        r.song.bpm if r.song.bpm is not None else "",
                        r.est_bpm if r.est_bpm is not None else "",
                        "" if r.acc1 is None else int(r.acc1),
                        "" if r.acc2 is None else int(r.acc2),
                        "" if r.time_ms is None else f"{r.time_ms:.0f}", r.error])


# ---------------------------------------------------------------------------------------------
# Lệnh
# ---------------------------------------------------------------------------------------------

def resolve_csv(args) -> tuple:
    """Trả (đường dẫn CSV, có phải bộ test không)."""
    if args.csv:
        path = Path(args.csv)
        return path, args.set == "test" or path.stem.lower().startswith("test")
    if not args.set:
        raise SystemExit("Cần --set dev|test hoặc --csv.")
    return DATA_DIR / f"{args.set}.csv", args.set == "test"


def load_and_run(args, csv_path: Path, extra: list) -> tuple:
    ds = dataset.load(csv_path)
    if ds.errors:
        for e in ds.errors:
            print(f"LỖI  {e}", file=sys.stderr)
        raise SystemExit(f"{csv_path}: {len(ds.errors)} lỗi — sửa CSV trước "
                         "(tools/keytest/validate_csv.py).")
    keylab = args.keylab or default_keylab()
    if not keylab:
        raise SystemExit("Không thấy keylab. Build: cmake -S engine -B engine/build/rel -G Ninja "
                         "-DCMAKE_BUILD_TYPE=Release -DTC_BUILD_TESTS=OFF && "
                         "cmake --build engine/build/rel --target keylab")
    cmd = shlex.split(keylab)
    raw = run_keylab(cmd, [str(s.path) for s in ds.songs], args.jobs, extra)
    results = [evaluate(s, raw.get(str(s.path))) for s in ds.songs]
    return results, cmd


def log_test_run(csv_path: Path, s: Summary) -> int:
    """Ghi lại lần chạy bộ test; trả về số thứ tự lần chạy."""
    log = csv_path.parent / "test-runs.jsonl"
    count = 0
    if log.is_file():
        count = sum(1 for line in log.read_text(encoding="utf-8").splitlines() if line.strip())
    entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "commit": git_info(),
             "csv": csv_path.name, "n": s.n, "score": round(s.score, 4)}
    with log.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    return count + 1


def cmd_run(args, extra: list) -> int:
    csv_path, is_test = resolve_csv(args)
    if is_test and extra:
        print("Từ chối: không chỉnh tham số keylab trên bộ test (chống học vẹt). Chỉnh trên "
              "--set dev, rồi đổi mặc định trong engine.", file=sys.stderr)
        return 2
    results, cmd = load_and_run(args, csv_path, extra)
    s = summarize(results)
    meta = [f"Thời điểm: {datetime.datetime.now():%Y-%m-%d %H:%M}, commit {git_info()}",
            f"Bộ dữ liệu: `{csv_path}` ({s.n} bài)",
            f"keylab: `{shlex.join([*cmd, 'batch', *extra])}`"]
    if is_test:
        meta.append(f"Lần chạy bộ test thứ {log_test_run(csv_path, s)} "
                    f"(nhật ký: `{csv_path.parent / 'test-runs.jsonl'}`)")
    report = render_report(f"Kết quả dò tone — {csv_path.stem}", results, s, meta)
    print(report)
    if args.out:
        Path(args.out).write_text(report, encoding="utf-8")
    if args.per_song:
        write_per_song(Path(args.per_song), results)
    if args.min_score is not None and s.score < args.min_score:
        print(f"Điểm {s.score:.3f} < {args.min_score}", file=sys.stderr)
        return 1
    return 0


def parse_grid(items: list) -> list:
    """['profile=kk,temperley', 'bass=0.5,1'] → [[('profile','kk'),('profile','temperley')], …]."""
    axes = []
    for item in items:
        name, sep, values = item.partition("=")
        if not sep or not name or not values:
            raise SystemExit(f"--grid không hợp lệ: {item!r} (dạng ten=gia_tri1,gia_tri2)")
        axes.append([(name.strip(), v.strip()) for v in values.split(",") if v.strip()])
    return axes


def cmd_sweep(args, extra: list) -> int:
    csv_path, is_test = resolve_csv(args)
    if is_test:
        print("Từ chối: chỉ quét tham số trên tập dev, không bao giờ trên bộ test.",
              file=sys.stderr)
        return 2
    combos = list(itertools.product(*parse_grid(args.grid))) if args.grid else [()]
    rows = []
    for combo in combos:
        params = [x for name, value in combo for x in (f"--{name}", value)]
        label = " ".join(f"{n}={v}" for n, v in combo) or "(mặc định)"
        print(f"→ {label}", file=sys.stderr)
        results, _ = load_and_run(args, csv_path, [*extra, *params])
        rows.append((label, summarize(results)))
    rows.sort(key=lambda r: -r[1].score)
    lines = [f"# Quét tham số — {csv_path.stem}", "",
             f"- Thời điểm: {datetime.datetime.now():%Y-%m-%d %H:%M}, commit {git_info()}", "",
             "| Tham số | MIREX | Tone chính | Timeline (bài chuyển tone) | Chuyển tone giả | "
             "BPM Acc1 / Acc2 |",
             "|---|---|---|---|---|---|"]
    for label, s in rows:
        lines.append(f"| {label} | {s.score:.3f} | {s.score_main:.3f} | {fmt(s.mod_score)} | "
                     f"{s.spurious}/{s.nomod_n} | {pct(s.acc1)} / {pct(s.acc2)} |")
    report = "\n".join(lines) + "\n"
    print(report)
    if args.out:
        Path(args.out).write_text(report, encoding="utf-8")
    return 0


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    extra: list = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]

    ap = argparse.ArgumentParser(description="Chấm điểm dò tone ToneCore (MIREX).")
    sub = ap.add_subparsers(dest="command", required=True)
    for name in ("run", "sweep"):
        p = sub.add_parser(name)
        p.add_argument("--set", choices=("dev", "test"))
        p.add_argument("--csv", help="file CSV nhãn (mặc định testdata/vn-key/<set>.csv)")
        p.add_argument("--keylab", help="lệnh chạy keylab (mặc định engine/build/rel|host)")
        p.add_argument("-j", "--jobs", type=int, default=None, help="số luồng keylab")
        p.add_argument("--out", help="ghi báo cáo Markdown ra file")
        if name == "run":
            p.add_argument("--per-song", help="ghi kết quả từng bài ra CSV")
            p.add_argument("--min-score", type=float, default=None,
                           help="thoát mã 1 nếu điểm MIREX thấp hơn")
        else:
            p.add_argument("--grid", action="append", default=[],
                           help="ten=gia_tri1,gia_tri2 → keylab --ten gia_tri (lặp được)")
    args = ap.parse_args(argv)
    if args.command == "run":
        return cmd_run(args, extra)
    return cmd_sweep(args, extra)


if __name__ == "__main__":
    sys.exit(main())
