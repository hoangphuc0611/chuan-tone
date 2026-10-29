"""Job tách giọng: nhận bài theo đoạn, tách theo cửa sổ ngay khi đủ dữ liệu (gối với upload —
docs/m6-plan.md, quyết định sau B1), trả stem giọng theo đúng ranh giới đoạn của bài gửi lên.

Ghép cửa sổ: cửa sổ k phủ [s, e) (dài W), cửa sổ k + 1 bắt đầu ở e − OV. Vùng chồng OV mẫu trộn
tuyến tính (giọng cửa sổ trước nhỏ dần, cửa sổ sau to dần) như cách demucs ghép các đoạn bên trong
một cửa sổ → không có vết nối. Phần [s, e − OV) của cửa sổ k chốt ngay; OV mẫu cuối giữ lại chờ cửa
sổ sau (cửa sổ cuối chốt tới hết bài).
"""

from __future__ import annotations

import enum
import secrets
import shutil
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .config import Settings


class JobError(Exception):
    """Lỗi do client (HTTP 4xx)."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


class State(str, enum.Enum):
    UPLOADING = "uploading"  # chưa nhận đủ các đoạn (vẫn tách phần đã có)
    SEPARATING = "separating"  # đã nhận đủ, đang tách nốt
    DONE = "done"
    FAILED = "failed"


@dataclass
class Job:
    id: str
    device: str
    sample_rate: int
    channels: int
    frames: int
    chunk_frames: int
    chunks: int
    dir: Path
    created: float = field(default_factory=time.time)
    state: State = State.UPLOADING
    error: str | None = None
    pcm: np.ndarray | None = None  # [kênh, mẫu] bài gốc
    vocals: np.ndarray | None = None  # [kênh, mẫu] giọng đã chốt
    have: list[bool] = field(default_factory=list)
    contiguous: int = 0  # số mẫu liền mạch từ đầu bài đã nhận
    next_start: int = 0  # đầu cửa sổ kế tiếp
    finalized: int = 0  # số mẫu giọng đã chốt
    tail: np.ndarray | None = None  # OV mẫu cuối của cửa sổ trước (chờ trộn)
    encoded: int = 0  # số đoạn giọng đã nén xong (liền từ đầu) — tải được
    busy: bool = False  # worker đang tách một cửa sổ của job này
    last_activity: float = field(default_factory=time.time)
    # Số đo (giây, tính từ lúc tạo job).
    upload_done_at: float | None = None
    done_at: float | None = None
    separate_seconds: float = 0.0

    def chunk_range(self, i: int) -> tuple[int, int]:
        start = i * self.chunk_frames
        return start, min(self.frames, start + self.chunk_frames)

    def chunks_received(self) -> int:
        return sum(self.have)

    def status(self) -> dict:
        def rel(t: float | None) -> float | None:
            return None if t is None else round(t - self.created, 2)

        return {
            "id": self.id,
            "state": self.state.value,
            "error": self.error,
            "progress": round(self.finalized / self.frames, 4) if self.frames else 1.0,
            "chunks": self.chunks,
            "chunks_received": self.chunks_received(),
            "vocals_ready": self.encoded,
            "timing": {
                "upload_done": rel(self.upload_done_at),
                "done": rel(self.done_at),
                "separate": round(self.separate_seconds, 2),
            },
        }


def next_window(job: Job, window: int, overlap: int) -> tuple[int, int] | None:
    """Cửa sổ kế tiếp tách được ngay (đã nhận đủ dữ liệu), hoặc None."""
    s = job.next_start
    if job.state not in (State.UPLOADING, State.SEPARATING) or s >= job.frames:
        return None
    e = min(job.frames, s + window)
    return (s, e) if job.contiguous >= e else None


def apply_window(job: Job, s: int, e: int, vocals: np.ndarray, overlap: int) -> None:
    """Ghép giọng của cửa sổ [s, e) vào job (trộn vùng chồng với cửa sổ trước), chốt tới e − overlap
    (hoặc tới hết bài nếu là cửa sổ cuối)."""
    assert vocals.shape == (job.channels, e - s)
    v = vocals.astype(np.float32, copy=True)
    if job.tail is not None:
        n = job.tail.shape[1]
        ramp = ((np.arange(n, dtype=np.float32) + 0.5) / n)[None, :]
        v[:, :n] = job.tail * (1.0 - ramp) + v[:, :n] * ramp
    end = e if e >= job.frames else e - overlap
    job.vocals[:, s:end] = v[:, : end - s]
    job.tail = v[:, end - s :].copy() if e < job.frames else None
    job.finalized = end
    job.next_start = end


def chunks_finalized(job: Job) -> int:
    """Số đoạn (liền từ đầu) có giọng đã chốt hết."""
    if job.finalized >= job.frames:
        return job.chunks
    return job.finalized // job.chunk_frames


class JobStore:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.jobs: dict[str, Job] = {}
        self.lock = threading.Lock()
        self.changed = threading.Condition(self.lock)  # có đoạn mới / job mới / job bị xoá
        self._per_device: dict[tuple[str, str], int] = {}
        settings.data_dir.mkdir(parents=True, exist_ok=True)

    # ---- Gọi từ HTTP ----

    def create(self, device: str, sample_rate: int, channels: int, frames: int, chunk_frames: int,
               chunks: int) -> Job:
        s = self.settings
        if sample_rate != s.sample_rate:
            raise JobError(400, f"cần tần số {s.sample_rate} Hz")
        if channels not in (1, 2):
            raise JobError(400, "chỉ nhận 1 hoặc 2 kênh")
        if frames <= 0 or chunk_frames <= 0:
            raise JobError(400, "độ dài sai")
        if frames / sample_rate > s.max_seconds:
            raise JobError(413, f"bài dài quá {int(s.max_seconds // 60)} phút")
        if chunks != -(-frames // chunk_frames):
            raise JobError(400, "số đoạn không khớp độ dài")
        if chunk_frames * channels * 2 > s.max_chunk_bytes:
            raise JobError(400, "đoạn dài quá")
        day = time.strftime("%Y-%m-%d", time.gmtime())
        with self.lock:
            used = self._per_device.get((device, day), 0)
            if used >= s.jobs_per_device_per_day:
                raise JobError(429, "hết lượt tách hôm nay")
            self._per_device[(device, day)] = used + 1
            job_id = secrets.token_urlsafe(16)
            d = s.data_dir / job_id
            d.mkdir(parents=True)
            job = Job(id=job_id, device=device, sample_rate=sample_rate, channels=channels,
                      frames=frames, chunk_frames=chunk_frames, chunks=chunks, dir=d)
            job.pcm = np.zeros((channels, frames), dtype=np.float32)
            job.vocals = np.zeros((channels, frames), dtype=np.float32)
            job.have = [False] * chunks
            self.jobs[job_id] = job
            self.changed.notify_all()
            return job

    def get(self, job_id: str, device: str) -> Job:
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None or job.device != device:
                raise JobError(404, "không có job này (có thể đã hết hạn)")
            return job

    def add_chunk(self, job_id: str, device: str, index: int, pcm: np.ndarray, rate: int) -> None:
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None or job.device != device:
                raise JobError(404, "không có job này (có thể đã hết hạn)")
            if job.state not in (State.UPLOADING, State.SEPARATING):
                raise JobError(409, "job đã kết thúc")
            if not 0 <= index < job.chunks:
                raise JobError(400, "số thứ tự đoạn sai")
            a, b = job.chunk_range(index)
            if rate != job.sample_rate or pcm.shape != (job.channels, b - a):
                raise JobError(400, "đoạn không khớp (tần số / số kênh / số mẫu)")
            if not job.have[index]:
                job.pcm[:, a:b] = pcm
                job.have[index] = True
                while job.contiguous < job.frames:
                    i = job.contiguous // job.chunk_frames
                    if not job.have[i]:
                        break
                    job.contiguous = job.chunk_range(i)[1]
            job.last_activity = time.time()
            if all(job.have) and job.state == State.UPLOADING:
                job.state = State.SEPARATING
                job.upload_done_at = time.time()
            self.changed.notify_all()

    def vocals_path(self, job: Job, index: int) -> Path:
        return job.dir / f"vocals_{index:03d}.flac"

    def delete(self, job_id: str, device: str | None = None) -> None:
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None or (device is not None and job.device != device):
                raise JobError(404, "không có job này")
            self._drop(job)

    def _drop(self, job: Job) -> None:
        # Gọi khi giữ lock. Worker đang tách job này sẽ thấy job không còn trong bảng và bỏ kết quả.
        self.jobs.pop(job.id, None)
        job.state = State.FAILED if job.state != State.DONE else job.state
        job.pcm = job.vocals = job.tail = None
        shutil.rmtree(job.dir, ignore_errors=True)
        self.changed.notify_all()

    # ---- Dọn dẹp ----

    def cleanup(self, now: float | None = None) -> int:
        """Xoá job quá hạn (24 giờ) hoặc bỏ dở (không nhận đoạn mới quá idle_seconds)."""
        now = time.time() if now is None else now
        s = self.settings
        removed = 0
        with self.lock:
            for job in list(self.jobs.values()):
                expired = now - job.created > s.ttl_seconds
                idle = job.state == State.UPLOADING and now - job.last_activity > s.idle_seconds
                if expired or idle:
                    self._drop(job)
                    removed += 1
            # Thư mục mồ côi (server khởi động lại): xoá luôn — job trong bộ nhớ đã mất.
            for d in s.data_dir.iterdir():
                if d.is_dir() and d.name not in self.jobs:
                    shutil.rmtree(d, ignore_errors=True)
        return removed
