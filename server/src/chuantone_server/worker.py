"""Luồng tách (một bộ tách, một GPU): lấy cửa sổ đã đủ dữ liệu của job cũ nhất, tách, ghép, nén
các đoạn giọng đã chốt. Luồng dọn dẹp: xoá job quá hạn mỗi phút."""

from __future__ import annotations

import logging
import threading
import time

from .config import Settings
from .flac import Flac
from .jobs import Job, JobStore, State, apply_window, chunks_finalized, next_window
from .separator import Separator

log = logging.getLogger("chuantone.server")


class SeparationWorker(threading.Thread):
    def __init__(self, store: JobStore, separator: Separator, flac: Flac, settings: Settings) -> None:
        super().__init__(name="tach-giong", daemon=True)
        self.store = store
        self.separator = separator
        self.flac = flac
        rate = settings.sample_rate
        self.window = int(settings.window_seconds * rate)
        self.overlap = int(settings.overlap_seconds * rate)
        self._stop = threading.Event()

    def stop(self) -> None:
        self._stop.set()
        with self.store.lock:
            self.store.changed.notify_all()

    def _pick(self) -> tuple[Job, int, int] | None:
        # Gọi khi giữ lock: job tạo sớm nhất có cửa sổ tách được ngay.
        for job in sorted(self.store.jobs.values(), key=lambda j: j.created):
            if job.busy:
                continue
            w = next_window(job, self.window, self.overlap)
            if w is not None:
                return job, w[0], w[1]
        return None

    def run(self) -> None:
        store = self.store
        while not self._stop.is_set():
            with store.lock:
                work = self._pick()
                if work is None:
                    store.changed.wait(timeout=1.0)
                    continue
                job, s, e = work
                job.busy = True
                audio = job.pcm[:, s:e].copy()
            try:
                t0 = time.perf_counter()
                vocals = self.separator.separate(audio)
                dt = time.perf_counter() - t0
                with store.lock:
                    if store.jobs.get(job.id) is not job:  # bị xoá trong lúc tách
                        continue
                    job.separate_seconds += dt
                    apply_window(job, s, e, vocals, self.overlap)
                    ready = chunks_finalized(job)
                    todo = [(i, job.chunk_range(i), job.vocals) for i in range(job.encoded, ready)]
                for i, (a, b), buf in todo:
                    self.flac.encode(buf[:, a:b], job.sample_rate, store.vocals_path(job, i))
                with store.lock:
                    if store.jobs.get(job.id) is not job:
                        continue
                    job.encoded = max(job.encoded, ready)
                    if job.encoded == job.chunks:
                        job.state = State.DONE
                        job.done_at = time.time()
                        job.pcm = job.vocals = job.tail = None  # giải phóng RAM, giữ file giọng
                        t = job.status()["timing"]
                        log.info("job %s xong: %.0f s âm thanh, nhận đủ sau %s s, xong sau %s s "
                                 "(tách %s s)", job.id[:6], job.frames / job.sample_rate,
                                 t["upload_done"], t["done"], t["separate"])
            except Exception as exc:  # noqa: BLE001 — job lỗi không được làm chết luồng tách
                log.exception("job %s lỗi", job.id[:6])
                with store.lock:
                    job.state = State.FAILED
                    job.error = f"tách lỗi: {type(exc).__name__}"
            finally:
                with store.lock:
                    job.busy = False
                    store.changed.notify_all()


class Cleaner(threading.Thread):
    def __init__(self, store: JobStore, every: float = 60.0) -> None:
        super().__init__(name="don-dep", daemon=True)
        self.store = store
        self.every = every
        self._stop = threading.Event()

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        while not self._stop.wait(self.every):
            n = self.store.cleanup()
            if n:
                log.info("đã xoá %d job hết hạn / bỏ dở", n)
