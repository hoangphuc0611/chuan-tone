"""Nén / giải mã FLAC bằng công cụ `tcflac` của engine (cùng bộ nén app dùng, không thêm thư viện
FLAC nào cho Python). Tìm theo thứ tự: biến CHUANTONE_TCFLAC → engine/build/*/tools/tcflac → PATH."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np

_REPO = Path(__file__).resolve().parents[3]


@dataclass
class FlacInfo:
    sample_rate: int
    channels: int
    frames: int


def find_tcflac() -> str | None:
    env = os.environ.get("CHUANTONE_TCFLAC")
    if env:
        return env
    for preset in ("host-release", "host"):
        p = _REPO / "engine" / "build" / preset / "tools" / "tcflac"
        if p.is_file():
            return str(p)
    return shutil.which("tcflac")


class Flac:
    def __init__(self, tool: str | None = None) -> None:
        self.tool = tool or find_tcflac()
        if not self.tool:
            raise RuntimeError("không tìm thấy tcflac (build engine: cmake --build --preset host)")

    def decode(self, data: bytes) -> tuple[np.ndarray, FlacInfo]:
        """FLAC → float32 [kênh, mẫu]."""
        with tempfile.TemporaryDirectory() as d:
            src, dst = Path(d) / "in.flac", Path(d) / "out.f32"
            src.write_bytes(data)
            out = subprocess.run([self.tool, "decode", str(src), str(dst)], capture_output=True,
                                 text=True, check=False)
            if out.returncode != 0:
                raise ValueError(out.stderr.strip() or "FLAC hỏng")
            info = FlacInfo(**json.loads(out.stdout))
            pcm = np.fromfile(dst, dtype="<f4")
        if info.channels <= 0 or pcm.size != info.frames * info.channels:
            raise ValueError("FLAC hỏng")
        return pcm.reshape(-1, info.channels).T.copy(), info

    def encode(self, audio: np.ndarray, sample_rate: int, path: Path) -> None:
        """float32 [kênh, mẫu] → FLAC 16-bit (làm tròn + kẹp), ghi qua tên tạm."""
        with tempfile.TemporaryDirectory() as d:
            raw = Path(d) / "in.f32"
            np.ascontiguousarray(audio.T, dtype="<f4").tofile(raw)
            out = subprocess.run(
                [self.tool, "encode", str(sample_rate), str(audio.shape[0]), str(raw), str(path)],
                capture_output=True, text=True, check=False)
            if out.returncode != 0:
                raise RuntimeError(out.stderr.strip() or "không nén được FLAC")
