"""Chạy server thật (uvicorn, bộ tách giả, tcflac của engine) và gọi như app: tạo job, gửi các đoạn
FLAC, theo dõi, tải giọng, xoá. Cần tcflac (cmake --build --preset host --target tcflac)."""

import json
import socket
import threading
import time
import urllib.error
import urllib.request

import numpy as np
import pytest
import uvicorn

from chuantone_server.app import create_app
from chuantone_server.config import Settings
from chuantone_server.flac import Flac, find_tcflac
from chuantone_server.separator import FakeSeparator

RATE = 44100
DEVICE = {"X-ChuanTone-Device": "may-test"}

pytestmark = pytest.mark.skipif(find_tcflac() is None, reason="chưa build tcflac")


def start_server(tmp_path, **kw):
    s = Settings(data_dir=tmp_path / "data", separator="fake")
    s.window_seconds, s.overlap_seconds = 1.5, 0.25
    for k, v in kw.items():
        setattr(s, k, v)
    app = create_app(s, FakeSeparator(), Flac())
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    srv = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="warning"))
    t = threading.Thread(target=srv.run, daemon=True)
    t.start()
    while not srv.started:
        time.sleep(0.01)

    def stop():
        srv.should_exit = True
        t.join(timeout=5)

    return f"http://127.0.0.1:{port}", app, s, stop


@pytest.fixture
def server(tmp_path):
    url, app, s, stop = start_server(tmp_path)
    yield url, app, s
    stop()


def call(method, url, body=None, headers=DEVICE, ctype="application/json"):
    data = json.dumps(body).encode() if isinstance(body, dict) else body
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={**headers, **({"Content-Type": ctype} if data else {})})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            raw = r.read()
            if raw and r.headers.get_content_type() == "application/json":
                return r.status, json.loads(raw)
            return r.status, raw
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def flac_bytes(flac, audio, tmp_path, name):
    p = tmp_path / name
    flac.encode(audio, RATE, p)
    return p.read_bytes()


def test_full_flow(server, tmp_path):
    url, app, settings = server
    flac = Flac()
    status, health = call("GET", f"{url}/v1/health", headers={})
    assert status == 200 and health["separator"] == "fake" and health["sample_rate"] == RATE

    frames, chunk = int(RATE * 6.3), RATE * 2
    rng = np.random.default_rng(1)
    audio = np.round(rng.standard_normal((2, frames)) * 0.1 * 32768) / 32768
    audio = audio.astype(np.float32)
    chunks = -(-frames // chunk)
    status, job = call("POST", f"{url}/v1/jobs", {"sample_rate": RATE, "channels": 2,
                                                   "frames": frames, "chunk_frames": chunk,
                                                   "chunks": chunks})
    assert status == 200, job
    jid = job["id"]
    assert job["state"] == "uploading" and job["chunks"] == chunks == 4

    for i in [1, 0, 3, 2]:  # lộn thứ tự
        a, b = i * chunk, min(frames, (i + 1) * chunk)
        status, _ = call("PUT", f"{url}/v1/jobs/{jid}/chunks/{i}",
                         flac_bytes(flac, audio[:, a:b], tmp_path, f"c{i}.flac"), ctype="audio/flac")
        assert status == 204

    deadline = time.time() + 20
    while True:
        status, st = call("GET", f"{url}/v1/jobs/{jid}")
        assert status == 200
        if st["state"] == "done":
            break
        assert st["state"] in ("uploading", "separating"), st
        assert time.time() < deadline
        time.sleep(0.05)
    assert st["vocals_ready"] == chunks and st["progress"] == 1.0
    assert st["timing"]["done"] is not None

    expected = FakeSeparator().separate(audio)
    got = []
    for i in range(chunks):
        status, data = call("GET", f"{url}/v1/jobs/{jid}/vocals/{i}")
        assert status == 200
        pcm, info = flac.decode(data)
        assert info.sample_rate == RATE and info.channels == 2
        got.append(pcm)
    np.testing.assert_allclose(np.concatenate(got, axis=1), expected, atol=1.5 / 32768)

    status, _ = call("DELETE", f"{url}/v1/jobs/{jid}")
    assert status == 204
    assert call("GET", f"{url}/v1/jobs/{jid}")[0] == 404
    assert not (settings.data_dir / jid).exists()


def test_errors(server, tmp_path):
    url, _, _ = server
    assert call("POST", f"{url}/v1/jobs", {"sample_rate": RATE, "channels": 2, "frames": RATE,
                                           "chunk_frames": RATE, "chunks": 1}, headers={})[0] == 400
    status, body = call("POST", f"{url}/v1/jobs", {"sample_rate": RATE, "channels": 2,
                                                   "frames": RATE * 700, "chunk_frames": RATE * 20,
                                                   "chunks": 35})
    assert status == 413 and "phút" in body["error"]
    status, job = call("POST", f"{url}/v1/jobs", {"sample_rate": RATE, "channels": 1,
                                                  "frames": RATE, "chunk_frames": RATE,
                                                  "chunks": 1})
    jid = job["id"]
    # Máy khác không thấy job; FLAC hỏng; sai số mẫu; giọng chưa xong.
    assert call("GET", f"{url}/v1/jobs/{jid}", headers={"X-ChuanTone-Device": "khac"})[0] == 404
    assert call("PUT", f"{url}/v1/jobs/{jid}/chunks/0", b"khong phai flac",
                ctype="audio/flac")[0] == 400
    short = flac_bytes(Flac(), np.zeros((1, 100), np.float32), tmp_path, "s.flac")
    assert call("PUT", f"{url}/v1/jobs/{jid}/chunks/0", short, ctype="audio/flac")[0] == 400
    assert call("GET", f"{url}/v1/jobs/{jid}/vocals/0")[0] == 409
    assert call("GET", f"{url}/v1/jobs/{jid}/vocals/5")[0] == 400


def test_api_key(tmp_path):
    url, _, _, stop = start_server(tmp_path, api_key="bi-mat")
    try:
        req = {"sample_rate": RATE, "channels": 1, "frames": RATE, "chunk_frames": RATE,
               "chunks": 1}
        assert call("POST", f"{url}/v1/jobs", req)[0] == 401
        ok = {**DEVICE, "Authorization": "Bearer bi-mat"}
        assert call("POST", f"{url}/v1/jobs", req, headers=ok)[0] == 200
        assert call("GET", f"{url}/v1/health", headers={})[0] == 200  # health không cần khoá
    finally:
        stop()
