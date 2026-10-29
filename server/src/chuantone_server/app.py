"""HTTP API v1 của server tách giọng (docs/m6-plan.md mục 1).

  GET    /v1/health                       → {"ok", "separator", "version", "sample_rate"}
  POST   /v1/jobs                         {"sample_rate","channels","frames","chunk_frames","chunks"}
  PUT    /v1/jobs/{id}/chunks/{i}         thân = FLAC của đoạn i (mẫu i·chunk_frames…)
  GET    /v1/jobs/{id}                    trạng thái, tiến độ, số đoạn giọng tải được, số đo thời gian
  GET    /v1/jobs/{id}/vocals/{i}         FLAC giọng của đoạn i (cùng ranh giới với đoạn gửi lên)
  DELETE /v1/jobs/{id}                    xoá ngay mọi file

Mọi yêu cầu (trừ health) cần header X-ChuanTone-Device (mã ngẫu nhiên của máy) và, nếu đặt
CHUANTONE_API_KEY, header Authorization: Bearer <khoá>. Không ghi log tên bài / nội dung âm thanh.
"""

from __future__ import annotations

import argparse
import contextlib
import logging
import socket

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel
from starlette.concurrency import run_in_threadpool

from . import __version__
from .config import Settings
from .flac import Flac
from .jobs import JobError, JobStore
from .separator import Separator, make_separator
from .worker import Cleaner, SeparationWorker

log = logging.getLogger("chuantone.server")


class JobRequest(BaseModel):
    sample_rate: int
    channels: int
    frames: int
    chunk_frames: int
    chunks: int


def create_app(settings: Settings, separator: Separator, flac: Flac) -> FastAPI:
    store = JobStore(settings)
    worker = SeparationWorker(store, separator, flac, settings)
    cleaner = Cleaner(store)

    @contextlib.asynccontextmanager
    async def lifespan(_app: FastAPI):
        store.cleanup()
        worker.start()
        cleaner.start()
        yield
        worker.stop()
        cleaner.stop()

    app = FastAPI(title="ChuẩnTone – tách giọng", version=__version__, lifespan=lifespan)
    app.state.store = store

    @app.exception_handler(JobError)
    async def job_error(_request: Request, exc: JobError):
        return JSONResponse(status_code=exc.status, content={"error": exc.message})

    def check(device: str | None, authorization: str | None) -> str:
        if settings.api_key and authorization != f"Bearer {settings.api_key}":
            raise HTTPException(401, "sai khoá")
        if not device or len(device) > 128:
            raise HTTPException(400, "thiếu X-ChuanTone-Device")
        return device

    @app.get("/v1/health")
    def health():
        return {"ok": True, "separator": separator.name, "version": __version__,
                "sample_rate": settings.sample_rate}

    @app.post("/v1/jobs")
    def create_job(req: JobRequest, x_chuantone_device: str | None = Header(None),
                   authorization: str | None = Header(None)):
        device = check(x_chuantone_device, authorization)
        job = store.create(device, req.sample_rate, req.channels, req.frames, req.chunk_frames,
                           req.chunks)
        log.info("job %s: %.0f s, %d đoạn", job.id[:6], req.frames / req.sample_rate, req.chunks)
        return job.status()

    @app.put("/v1/jobs/{job_id}/chunks/{index}", status_code=204)
    async def put_chunk(job_id: str, index: int, request: Request,
                        x_chuantone_device: str | None = Header(None),
                        authorization: str | None = Header(None)):
        device = check(x_chuantone_device, authorization)
        store.get(job_id, device)  # 404 sớm, trước khi nhận cả đoạn
        body = bytearray()
        async for part in request.stream():
            body += part
            if len(body) > settings.max_chunk_bytes:
                raise HTTPException(413, "đoạn lớn quá")
        try:
            pcm, info = await run_in_threadpool(flac.decode, bytes(body))
        except ValueError as exc:
            raise HTTPException(400, f"không đọc được FLAC: {exc}") from exc
        store.add_chunk(job_id, device, index, pcm, info.sample_rate)

    @app.get("/v1/jobs/{job_id}")
    def get_job(job_id: str, x_chuantone_device: str | None = Header(None),
                authorization: str | None = Header(None)):
        job = store.get(job_id, check(x_chuantone_device, authorization))
        with store.lock:
            return job.status()

    @app.get("/v1/jobs/{job_id}/vocals/{index}")
    def get_vocals(job_id: str, index: int, x_chuantone_device: str | None = Header(None),
                   authorization: str | None = Header(None)):
        job = store.get(job_id, check(x_chuantone_device, authorization))
        with store.lock:
            if not 0 <= index < job.chunks:
                raise HTTPException(400, "số thứ tự đoạn sai")
            if index >= job.encoded:
                raise HTTPException(409, "đoạn giọng chưa xong")
            path = store.vocals_path(job, index)
        return FileResponse(path, media_type="audio/flac")

    @app.delete("/v1/jobs/{job_id}", status_code=204)
    def delete_job(job_id: str, x_chuantone_device: str | None = Header(None),
                   authorization: str | None = Header(None)):
        store.delete(job_id, check(x_chuantone_device, authorization))

    return app


def lan_address() -> str:
    """Địa chỉ IP trong mạng Wi-Fi (để nhập vào app). UDP connect không gửi gói nào."""
    with contextlib.suppress(OSError), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    return "127.0.0.1"


def main() -> None:
    import uvicorn

    ap = argparse.ArgumentParser(description="Server tách giọng ChuẩnTone (chỉ phát triển nội bộ)")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--separator", default=None, help="htdemucs (mặc định) / fake")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    settings = Settings()
    if args.separator:
        settings.separator = args.separator
    kwargs = {"overlap": 0.1} if settings.separator == "htdemucs" else {}
    log.info("nạp bộ tách %s…", settings.separator)
    separator = make_separator(settings.separator, **kwargs)
    app = create_app(settings, separator, Flac())
    log.info("server sẵn sàng — nhập vào app: http://%s:%d", lan_address(), args.port)
    uvicorn.run(app, host=args.host, port=args.port, access_log=False, log_level="warning")
