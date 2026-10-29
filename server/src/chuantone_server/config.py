"""Cấu hình server (biến môi trường CHUANTONE_*), giá trị mặc định theo docs/m6-plan.md."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path


def _env(name: str, default: str) -> str:
    return os.environ.get(f"CHUANTONE_{name}", default)


@dataclass
class Settings:
    data_dir: Path = field(default_factory=lambda: Path(_env("DATA_DIR", "~/.chuantone-server")).expanduser())
    separator: str = field(default_factory=lambda: _env("SEPARATOR", "htdemucs"))
    sample_rate: int = 44100  # tần số model; app xuất đúng tần số này
    max_seconds: float = 600.0  # bài dài nhất (mục 4 của spec: 10 phút)
    max_chunk_bytes: int = 32 * 1024 * 1024
    ttl_seconds: float = 24 * 3600.0  # tự xoá mọi file sau chừng này
    idle_seconds: float = 15 * 60.0  # job không nhận thêm đoạn nào → huỷ
    jobs_per_device_per_day: int = 50
    # Tách theo cửa sổ dài window_seconds, hai cửa sổ liền nhau chồng overlap_seconds (crossfade).
    window_seconds: float = 30.0
    overlap_seconds: float = 2.0
    api_key: str | None = field(default_factory=lambda: os.environ.get("CHUANTONE_API_KEY") or None)
