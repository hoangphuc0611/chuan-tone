"""Giả lập app gửi một bài lên server và đo thời gian từng bước (docs/m6-plan.md B3 / B7).

  uv run python tools/client_sim.py http://127.0.0.1:8000 bai.m4a [--mbps 20] [--chunk 20]

Như app: tcflac export (giải mã + FLAC theo đoạn 44,1 kHz) → upload từng đoạn ngay khi có (tốc độ
mạng giả lập bằng --mbps) → hỏi trạng thái → tải giọng từng đoạn ngay khi xong → xoá job.
In: thời điểm xuất xong, upload xong, tách xong, tải xong; ghi stem giọng ghép lại nếu có --out.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import numpy as np

from chuantone_server.flac import Flac, find_tcflac

DEVICE = {"X-ChuanTone-Device": "client-sim"}


def request(method: str, url: str, data: bytes | None = None, ctype: str | None = None):
    headers = dict(DEVICE)
    if ctype:
        headers["Content-Type"] = ctype
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    with urllib.request.urlopen(req, timeout=60) as r:
        body = r.read()
        return json.loads(body) if body and r.headers.get_content_type() == "application/json" else body


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("input", type=Path)
    ap.add_argument("--chunk", type=float, default=20.0, help="giây mỗi đoạn")
    ap.add_argument("--mbps", type=float, default=0.0, help="giả lập tốc độ upload (0 = không giới hạn)")
    ap.add_argument("--out", type=Path, default=None, help="ghi stem giọng (FLAC)")
    args = ap.parse_args()

    flac = Flac()
    t0 = time.perf_counter()
    marks: dict[str, float] = {}

    def mark(name: str) -> None:
        marks.setdefault(name, time.perf_counter() - t0)

    with tempfile.TemporaryDirectory() as d:
        prefix = Path(d) / "bai"
        # tcflac export chạy song song; các đoạn hiện ra dần (ghi qua tên tạm).
        proc = subprocess.Popen([find_tcflac(), "export", str(args.input), str(prefix), "44100",
                                 str(args.chunk)], stdout=subprocess.PIPE, text=True)
        first = Path(f"{prefix}_000.flac")
        while not first.exists():
            if proc.poll() is not None and not first.exists():
                raise SystemExit("tcflac export lỗi")
            time.sleep(0.01)
        _, info = flac.decode(first.read_bytes())
        proc.wait()
        mark("xuất FLAC xong")
        files = sorted(Path(d).glob("bai_*.flac"))
        sizes = [f.stat().st_size for f in files]
        # Độ dài thật: giải mã đoạn cuối.
        last, _ = flac.decode(files[-1].read_bytes())
        chunk_frames = int(round(args.chunk * 44100))
        frames = chunk_frames * (len(files) - 1) + last.shape[1]
        job = request("POST", f"{args.url}/v1/jobs", json.dumps({
            "sample_rate": 44100, "channels": info.channels, "frames": frames,
            "chunk_frames": chunk_frames, "chunks": len(files)}).encode(), "application/json")
        jid = job["id"]

        def upload() -> None:
            for i, f in enumerate(files):
                data = f.read_bytes()
                start = time.perf_counter()
                request("PUT", f"{args.url}/v1/jobs/{jid}/chunks/{i}", data, "audio/flac")
                if args.mbps > 0:  # bù cho đủ thời gian của mạng chậm
                    wait = len(data) * 8 / (args.mbps * 1e6) - (time.perf_counter() - start)
                    if wait > 0:
                        time.sleep(wait)
            mark("upload xong")

        up = threading.Thread(target=upload)
        up.start()
        vocals: list[np.ndarray] = []
        while len(vocals) < len(files):
            st = request("GET", f"{args.url}/v1/jobs/{jid}")
            if st["state"] == "failed":
                raise SystemExit(f"server lỗi: {st['error']}")
            while len(vocals) < st["vocals_ready"]:
                if not vocals:
                    mark("có đoạn giọng đầu")
                data = request("GET", f"{args.url}/v1/jobs/{jid}/vocals/{len(vocals)}")
                vocals.append(flac.decode(data)[0])
            time.sleep(0.2)
        mark("tải giọng xong")
        up.join()
        server_timing = request("GET", f"{args.url}/v1/jobs/{jid}")["timing"]
        request("DELETE", f"{args.url}/v1/jobs/{jid}")

    secs = frames / 44100
    mb = sum(sizes) / 1e6
    print(f"bài {secs:.0f} s, {len(files)} đoạn, FLAC {mb:.1f} MB, mạng "
          f"{'không giới hạn' if args.mbps <= 0 else f'{args.mbps:g} Mbit/s'}")
    for k, v in marks.items():
        print(f"  {k:20} {v:6.1f} s")
    print(f"  server: tách {server_timing['separate']} s, nhận đủ sau {server_timing['upload_done']} s,"
          f" xong sau {server_timing['done']} s (tính từ lúc tạo job)")
    if args.out:
        flac.encode(np.concatenate(vocals, axis=1), 44100, args.out)


if __name__ == "__main__":
    main()
