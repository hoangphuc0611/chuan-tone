#!/usr/bin/env python3
"""Trang nghe A/B mù cho đổi tone beat (M4): dựng file nghe từ beat thật và chấm kết quả.

    python3 tools/stretch/ab.py build --list beats.txt --out testdata/beat-ab \\
        [--levels -6,-4,-2,2,4,6] [--algos pv,signalsmith] [--dur 30]
    python3 tools/stretch/ab.py score testdata/beat-ab/ket-qua.json

`beats.txt`: mỗi dòng `đường_dẫn [giây_bắt_đầu]` (mặc định bắt đầu ở 30% bài). Dòng trống / `#`
bỏ qua. Đường dẫn tương đối tính từ gốc repo.

Mỗi mục nghe = một beat ở một mức tone: một bản "gốc" (chưa đổi tone, để biết trống đúng ra nghe
thế nào) + các bản đã đổi tone bằng từng thuật toán, xếp ngẫu nhiên thành A / B / … và đặt tên
file ngẫu nhiên → người nghe không biết bản nào của ai. Đáp án nằm riêng trong `dap-an.json`
(trang không đọc file này). Người nghe bấm "Tải kết quả" để lưu `ket-qua.json`, rồi `score` ghép
với đáp án.

Audio là beat có bản quyền: chỉ dựng vào testdata/ (đã .gitignore), chỉ mở trên máy, không upload.
"""

from __future__ import annotations

import argparse
import html
import json
import random
import secrets
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BEATLAB = ROOT / "engine/build/rel/tools/beatlab"
ANSWERS = "dap-an.json"
QUESTIONS = {
    "nhoe": "Trống có nhòe / đánh đôi không?",
    "meo": "Có méo, rè, tiếng “phơ” / vang ảo không?",
}
CHOICES = ["Không", "Hơi", "Rõ"]


def parse_list(text: str) -> list:
    """Dòng `đường_dẫn [giây]` → [(Path, giây | None)]."""
    beats = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.rsplit(maxsplit=1)
        start = None
        if len(parts) == 2:
            try:
                start = float(parts[1])
                line = parts[0]
            except ValueError:
                pass
        path = Path(line)
        beats.append((path if path.is_absolute() else ROOT / path, start))
    return beats


def render(beatlab: str, src: Path, out: Path, semitones: float, algo: str, start: float,
           dur: float) -> dict:
    cmd = [beatlab, "render", str(src), "-o", str(out), "--semitones", str(semitones),
           "--algo", algo, "--start", f"{start:.3f}", "--dur", f"{dur:.3f}"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"beatlab lỗi ({src.name}, {algo} {semitones:+g}): {r.stderr.strip()}")
    return json.loads(r.stdout.strip().splitlines()[-1]) if r.stdout.strip() else {}


def song_duration(beatlab: str, src: Path, tmp: Path) -> float:
    """Độ dài cả bài (giây): render 0,1 s đầu ở mức 0 rồi đọc trường song_sec."""
    info = render(beatlab, src, tmp, 0, "pv", 0.0, 0.1)
    tmp.unlink(missing_ok=True)
    return float(info.get("song_sec", 0.0))


def build_items(beats: list, levels: list, algos: list, rng: random.Random) -> list:
    """Danh sách mục nghe (chưa render): mỗi (beat, mức) một mục, thứ tự thuật toán xáo trộn."""
    items = []
    for b, _ in enumerate(beats):
        for n in levels:
            order = list(algos)
            rng.shuffle(order)
            items.append({"id": secrets.token_hex(4), "beat": b, "level": n,
                          "labels": {chr(ord("A") + i): a for i, a in enumerate(order)}})
    rng.shuffle(items)
    return items


def page(items: list, beat_names: list, clips: dict) -> str:
    """HTML trang nghe — chỉ có tên file ngẫu nhiên, không có đáp án."""
    def radios(item_id: str, label: str, key: str) -> str:
        name = f"{item_id}.{label}.{key}"
        return "".join(
            f'<label><input type="radio" name="{name}" value="{c}"> {c}</label>' for c in CHOICES)

    blocks = []
    for i, it in enumerate(items, 1):
        rows = []
        for label in it["labels"]:
            q = "".join(f'<div class="q"><span>{html.escape(text)}</span>{radios(it["id"], label, key)}</div>'
                        for key, text in QUESTIONS.items())
            rows.append(f'<div class="clip"><h3>Bản {label}</h3>'
                        f'<audio controls preload="none" src="{clips[(it["id"], label)]}"></audio>{q}</div>')
        prefs = "".join(f'<label><input type="radio" name="{it["id"]}.thich" value="{v}"> {v}</label>'
                        for v in [*it["labels"], "Như nhau"])
        blocks.append(
            f'<section data-id="{it["id"]}"><h2>{i}. Beat {it["beat"] + 1} · tone '
            f'{it["level"]:+g}</h2><div class="clip ref"><h3>Gốc (chưa đổi tone)</h3>'
            f'<audio controls preload="none" src="{clips[(it["id"], "goc")]}"></audio></div>'
            f'{"".join(rows)}<div class="q"><span>Bản nào nghe tốt hơn?</span>{prefs}</div></section>')
    names = "".join(f"<li>Beat {i + 1}: {html.escape(n)}</li>" for i, n in enumerate(beat_names))
    return f"""<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Nghe A/B đổi tone</title>
<style>
:root {{ color-scheme: light dark; --bg: #fff; --fg: #1d1d1f; --muted: #6e6e73; --card: #f5f5f7; }}
@media (prefers-color-scheme: dark) {{ :root {{ --bg: #111; --fg: #f2f2f2; --muted: #a1a1a6; --card: #1e1e20; }} }}
body {{ background: var(--bg); color: var(--fg); font: 16px/1.5 system-ui, sans-serif; margin: 0 auto;
       max-width: 860px; padding: 16px; }}
section {{ background: var(--card); border-radius: 12px; padding: 12px 16px; margin: 16px 0; }}
h2 {{ font-size: 18px; margin: 4px 0 8px; }} h3 {{ font-size: 15px; margin: 8px 0 4px; }}
audio {{ width: 100%; }} .q {{ display: flex; flex-wrap: wrap; gap: 4px 14px; margin: 4px 0; }}
.q span {{ flex-basis: 100%; color: var(--muted); font-size: 14px; }} .ref h3 {{ color: var(--muted); }}
button {{ font-size: 16px; padding: 10px 18px; border-radius: 10px; }}
#bar {{ position: sticky; top: 0; background: var(--bg); padding: 8px 0; display: flex; gap: 12px;
       align-items: center; }}
</style></head><body>
<h1>Nghe A/B đổi tone beat</h1>
<p>Mỗi mục: nghe bản <b>Gốc</b> để biết trống đúng ra thế nào, rồi nghe các bản đã đổi tone (không ghi
tên thuật toán). Nên đeo tai nghe. Ở mức ±5…6 màu tiếng nhạc cụ đổi là bình thường — chỉ chấm nhòe
trống, tiếng méo / rè / "phơ". Chọn xong bấm <b>Tải kết quả</b> và gửi file <code>ket-qua.json</code>.</p>
<div id="bar"><button id="save">Tải kết quả</button><span id="done"></span></div>
{"".join(blocks)}
<details><summary>Danh sách beat</summary><ul>{names}</ul></details>
<script>
const KEY = "nghe-ab-" + location.pathname;
function collect() {{
  const out = {{}};
  document.querySelectorAll("input[type=radio]:checked").forEach(r => {{ out[r.name] = r.value; }});
  return out;
}}
function update() {{
  const s = collect();
  try {{ localStorage.setItem(KEY, JSON.stringify(s)); }} catch (e) {{}}
  const total = document.querySelectorAll("section").length;
  const done = [...document.querySelectorAll("section")].filter(
    sec => sec.querySelector("input[name$='.thich']:checked")).length;
  document.getElementById("done").textContent = done + "/" + total + " mục đã chọn bản tốt hơn";
}}
try {{
  const saved = JSON.parse(localStorage.getItem(KEY) || "{{}}");
  for (const [name, value] of Object.entries(saved)) {{
    const el = document.querySelector(`input[name="${{CSS.escape(name)}}"][value="${{CSS.escape(value)}}"]`);
    if (el) el.checked = true;
  }}
}} catch (e) {{}}
document.addEventListener("change", update);
update();
document.getElementById("save").onclick = () => {{
  const blob = new Blob([JSON.stringify(collect(), null, 1)], {{ type: "application/json" }});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "ket-qua.json";
  a.click();
}};
</script></body></html>
"""


def cmd_build(args) -> int:
    beats = parse_list(Path(args.list).read_text(encoding="utf-8"))
    if not beats:
        print("Danh sách beat trống.", file=sys.stderr)
        return 1
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    levels = [float(v) for v in args.levels.split(",")]
    algos = args.algos.split(",")
    rng = random.Random(args.seed)
    items = build_items(beats, levels, algos, rng)
    starts = []
    for src, start in beats:
        if start is None:
            start = 0.3 * song_duration(args.beatlab, src, out / "_tmp.wav")
        starts.append(start)
    clips = {}
    goc = {}
    for b, (src, _) in enumerate(beats):
        name = f"{secrets.token_hex(6)}.wav"
        render(args.beatlab, src, out / name, 0, "pv", starts[b], args.dur)
        goc[b] = name
    for it in items:
        clips[(it["id"], "goc")] = goc[it["beat"]]
        for label, algo in it["labels"].items():
            name = f"{secrets.token_hex(6)}.wav"
            src, _ = beats[it["beat"]]
            render(args.beatlab, src, out / name, it["level"], algo, starts[it["beat"]], args.dur)
            clips[(it["id"], label)] = name
        print(f"  beat {it['beat'] + 1} {it['level']:+g}: xong", file=sys.stderr)
    beat_names = [src.stem for src, _ in beats]
    (out / "index.html").write_text(page(items, beat_names, clips), encoding="utf-8")
    answers = {"beats": [{"file": str(src), "start": s} for (src, _), s in zip(beats, starts)],
               "dur": args.dur, "items": items}
    (out / ANSWERS).write_text(json.dumps(answers, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"Đã dựng {len(items)} mục nghe: {out / 'index.html'}")
    return 0


def summarize(results: dict, answers: dict) -> dict:
    """{(thuật toán, mức): {"n", "nhoe": [Không, Hơi, Rõ], "meo": [...], "thang", "hoa"}}."""
    table: dict = {}
    for it in answers["items"]:
        for label, algo in it["labels"].items():
            row = table.setdefault((algo, it["level"]), {
                "n": 0, "nhoe": [0, 0, 0], "meo": [0, 0, 0], "thang": 0, "hoa": 0})
            answered = False
            for key in QUESTIONS:
                v = results.get(f"{it['id']}.{label}.{key}")
                if v in CHOICES:
                    row[key][CHOICES.index(v)] += 1
                    answered = True
            pref = results.get(f"{it['id']}.thich")
            if pref == label:
                row["thang"] += 1
            elif pref == "Như nhau":
                row["hoa"] += 1
            if answered or pref:
                row["n"] += 1
    return table


def cmd_score(args) -> int:
    results = json.loads(Path(args.results).read_text(encoding="utf-8"))
    answers_path = Path(args.answers) if args.answers else Path(args.results).parent / ANSWERS
    answers = json.loads(answers_path.read_text(encoding="utf-8"))
    table = summarize(results, answers)
    print("| Thuật toán | Mức | Số mục | Nhòe trống: không / hơi / rõ | Méo, rè, phơ: không / hơi / rõ"
          " | Được chọn hơn | Như nhau |")
    print("|---|---|---|---|---|---|---|")
    for (algo, level), r in sorted(table.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        print(f"| {algo} | {level:+g} | {r['n']} | {' / '.join(map(str, r['nhoe']))} | "
              f"{' / '.join(map(str, r['meo']))} | {r['thang']} | {r['hoa']} |")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--list", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--levels", default="-6,-4,-2,2,4,6")
    b.add_argument("--algos", default="pv,signalsmith")
    b.add_argument("--dur", type=float, default=30.0)
    b.add_argument("--seed", type=int, default=None)
    b.add_argument("--beatlab", default=str(DEFAULT_BEATLAB))
    s = sub.add_parser("score")
    s.add_argument("results")
    s.add_argument("--answers")
    argv = list(sys.argv[1:] if argv is None else argv)
    # "--levels -6,-4" → argparse tưởng "-6,-4" là một tuỳ chọn; ghép thành "--levels=-6,-4".
    for i in range(len(argv) - 1):
        if argv[i] == "--levels":
            argv[i:i + 2] = [f"--levels={argv[i + 1]}", ""]
    args = ap.parse_args([a for a in argv if a != ""])
    return cmd_build(args) if args.cmd == "build" else cmd_score(args)


if __name__ == "__main__":
    sys.exit(main())
