#!/usr/bin/env python3
"""Trang nghe preset FX giọng (M5 B6) — chỉnh bằng tai.

    python3 tools/fx/fxpage.py build [--out testdata/fx-ab] [--fxlab engine/build/host/tools/fxlab]
    python3 tools/fx/fxpage.py score testdata/fx-ab/ket-qua.json

Mỗi preset: giọng hát khô (vocadito, CC BY 4.0) và cùng giọng qua preset, render bằng `fxlab render
--preset` (đúng chuỗi của engine: HPF 80 Hz → EQ → nén → de-esser → delay → reverb), đã chuẩn hoá
−23 LUFS (mức hát thật). Không mù — đây là chỉnh "chất" của từng preset. Người nghe chấm vài câu hỏi
+ ghi chú, bấm "Tải kết quả" để lưu ket-qua.json; `score` in bảng tổng hợp.
"""

from __future__ import annotations

import argparse
import html
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FXLAB = ROOT / "engine/build/host/tools/fxlab"

# (tên hiển thị, khoá fxlab, BPM điển hình cho delay theo nhịp, mô tả)
PRESETS = [
    ("Bolero / Trữ tình", "bolero", 76, "Tự nhiên · EQ +2 dB 3 kHz, −2 dB 300 Hz · nén 3:1 · vọng 1/4 nốt 25% · Hall 2,2 s 22%"),
    ("Ballad", "ballad", 72, "EQ +1,5 dB 4 kHz · nén 3:1 · vọng 1/8 15% · Plate 1,8 s 20%"),
    ("Nhạc trẻ", "pop", 100, "EQ +3 dB 5 kHz, air +2 dB 12 kHz · nén 4:1 · vọng 1/8 18% · Plate 1,4 s 15%"),
    ("Remix", "remix", 126, "EQ +3 dB 5 kHz · nén 5:1 · vọng 1/8 chấm 22% · Room 1,0 s 12%"),
    ("Rap", "rap", 90, "EQ +2 dB 2,5 kHz · nén 6:1 · không vọng · Room 0,6 s 8%"),
    ("Lofi", "lofi", 80, "cắt cao 9 kHz, cắt trầm 150 Hz · nén 3:1 · vọng 1/4 tối 20% · Room 1,2 s 18%"),
]
# (track vocadito, giây bắt đầu, nhãn)
VOICES = [(6, 0.0, "Giọng nữ (tiếng Anh)"), (35, 0.0, "Giọng nam (tiếng Quan Thoại)")]
DUR = 15.0
QUESTIONS = [
    ("vang", "Độ vang", ["Thiếu", "Vừa", "Quá"]),
    ("mau", "Màu giọng", ["Tối / đục", "Vừa", "Chói"]),
    ("nen", "Độ nén", ["Ít", "Vừa", "Bóp quá"]),
    ("s", "Tiếng s / x / ch", ["Còn chói", "Vừa", "Ngọng"]),
    ("dat", "Preset này", ["Đạt", "Chưa đạt"]),
]


def render(fxlab: Path, track: int, start: float, preset: str, bpm: int, out: Path) -> dict:
    src = ROOT / f"testdata/Audio/vocadito_{track}.wav"
    cmd = [str(fxlab), "render", str(src), "-o", str(out), "--start", f"{start}", "--dur", f"{DUR}",
           "--normalize-lufs", "-23", "--preset", preset, "--bpm", str(bpm)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def page(rows: list, voices: list) -> str:
    def radios(key: str, q: tuple) -> str:
        name = f"{key}.{q[0]}"
        return "".join(f'<label><input type="radio" name="{name}" value="{html.escape(c)}"> {html.escape(c)}</label>'
                        for c in q[2])

    blocks = []
    for r in rows:
        audio = "".join(
            f'<div class="v"><b>{html.escape(v)}</b><br>Khô <audio controls preload="none" src="{r["dry"][i]}"></audio>'
            f'<br>{html.escape(r["title"])} <audio controls preload="none" src="{r["wet"][i]}"></audio></div>'
            for i, v in enumerate(voices))
        qs = "".join(f'<div class="q">{html.escape(q[1])}: {radios(r["key"], q)}</div>' for q in QUESTIONS)
        blocks.append(
            f'<section><h2>{html.escape(r["title"])}</h2><p class="d">{html.escape(r["desc"])} · độ to so '
            f'với khô {r["dlufs"]:+.1f} LU</p>{audio}{qs}'
            f'<textarea name="{r["key"]}.ghichu" placeholder="Ghi chú (vd. vang đuôi dài quá, giọng hơi mỏng…)"></textarea></section>')
    return f"""<!doctype html><html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Nghe preset FX</title>
<style>
:root {{ --bg:#fbfaf8; --fg:#1d1b19; --muted:#6b645c; --card:#fff; --line:#e6e1da; --accent:#a33b20; }}
@media (prefers-color-scheme: dark) {{ :root {{ --bg:#171513; --fg:#ece7e1; --muted:#a39b91; --card:#211e1b; --line:#3a3530; --accent:#e0805f; }} }}
body {{ background:var(--bg); color:var(--fg); font:16px/1.5 system-ui,sans-serif; margin:0 auto; max-width:860px; padding:16px; }}
section {{ background:var(--card); border:1px solid var(--line); border-radius:10px; padding:12px 16px; margin:14px 0; }}
h1 {{ font-size:22px; }} h2 {{ font-size:18px; margin:4px 0; }} .d {{ color:var(--muted); font-size:14px; margin:0 0 8px; }}
.v {{ margin:8px 0; }} audio {{ width:100%; max-width:520px; vertical-align:middle; }}
.q {{ margin:6px 0; }} .q label {{ margin-right:12px; white-space:nowrap; }}
textarea {{ width:100%; min-height:48px; box-sizing:border-box; background:var(--bg); color:var(--fg); border:1px solid var(--line); border-radius:6px; }}
button {{ background:var(--accent); color:#fff; border:0; border-radius:8px; padding:10px 18px; font-size:16px; }}
.n {{ color:var(--muted); font-size:13px; }}
</style></head><body>
<h1>Nghe preset FX giọng (M5)</h1>
<p>Mỗi preset: nghe <b>Khô</b> rồi <b>qua preset</b> (đeo tai nghe). Chọn ý kiến và ghi chú, xong bấm
<b>Tải kết quả</b> rồi kéo file <code>ket-qua.json</code> vào thư mục <code>testdata/fx-ab/</code>.</p>
<p class="n">Âm lượng đã cân để preset to ≈ như khô. Delay chạy theo nhịp điển hình của thể loại. Giọng hát:
vocadito — R. M. Bittner et al., 2021, giấy phép CC BY 4.0.</p>
{''.join(blocks)}
<p><button onclick="save()">Tải kết quả</button></p>
<script>
function save() {{
  const r = {{}};
  document.querySelectorAll('input[type=radio]:checked').forEach(e => r[e.name] = e.value);
  document.querySelectorAll('textarea').forEach(e => {{ if (e.value.trim()) r[e.name] = e.value.trim(); }});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(r, null, 1)], {{type: 'application/json'}}));
  a.download = 'ket-qua.json';
  a.click();
}}
</script></body></html>"""


def cmd_build(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    fxlab = Path(args.fxlab)
    rows = []
    dry = []
    for track, start, _ in VOICES:
        name = f"kho_{track}.wav"
        render(fxlab, track, start, "dry", 100, out / name)
        dry.append(name)
    for title, key, bpm, desc in PRESETS:
        wet, d = [], []
        for track, start, _ in VOICES:
            name = f"{key}_{track}.wav"
            j = render(fxlab, track, start, key, bpm, out / name)
            wet.append(name)
            d.append(j["lufs_out"] - j["lufs_in"])
        rows.append({"title": title, "key": key, "desc": desc, "dry": dry, "wet": wet,
                     "dlufs": sum(d) / len(d)})
        print(f"  {title}: xong", file=sys.stderr)
    (out / "index.html").write_text(page(rows, [v[2] for v in VOICES]), encoding="utf-8")
    print(f"Đã dựng {len(rows)} preset: {out / 'index.html'}")
    return 0


def cmd_score(args) -> int:
    r = json.loads(Path(args.results).read_text(encoding="utf-8"))
    print("| Preset | " + " | ".join(q[1] for q in QUESTIONS) + " | Ghi chú |")
    print("|---|" + "---|" * (len(QUESTIONS) + 1))
    for title, key, _, _ in PRESETS:
        cells = [r.get(f"{key}.{q[0]}", "—") for q in QUESTIONS]
        print(f"| {title} | " + " | ".join(cells) + f" | {r.get(f'{key}.ghichu', '')} |")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--out", default=str(ROOT / "testdata/fx-ab"))
    b.add_argument("--fxlab", default=str(DEFAULT_FXLAB))
    b.set_defaults(func=cmd_build)
    s = sub.add_parser("score")
    s.add_argument("results")
    s.set_defaults(func=cmd_score)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
