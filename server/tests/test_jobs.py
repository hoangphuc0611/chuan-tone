import time

import numpy as np
import pytest

from chuantone_server.config import Settings
from chuantone_server.jobs import (JobError, JobStore, State, apply_window, chunks_finalized,
                                   next_window)
from chuantone_server.separator import FakeSeparator

RATE = 44100


def settings(tmp_path, **kw):
    s = Settings(data_dir=tmp_path / "data", separator="fake")
    for k, v in kw.items():
        setattr(s, k, v)
    return s


def song(frames, channels=2, seed=0):
    rng = np.random.default_rng(seed)
    return (rng.standard_normal((channels, frames)) * 0.2).astype(np.float32)


def run_windows(store, job, sep, window, overlap):
    """Như worker nhưng chạy đồng bộ: tách mọi cửa sổ đã đủ dữ liệu."""
    while (w := next_window(job, window, overlap)) is not None:
        s, e = w
        apply_window(job, s, e, sep.separate(job.pcm[:, s:e]), overlap)


@pytest.mark.parametrize("frames,chunk,window,overlap", [
    (RATE * 7 + 123, RATE * 2, RATE * 3, RATE // 4),
    (RATE * 3, RATE, RATE * 3, RATE // 2),  # đúng một cửa sổ
    (RATE * 5, RATE * 5, RATE, RATE // 10),  # một đoạn, nhiều cửa sổ
    (1000, 300, 400, 50),
])
def test_streaming_matches_one_shot(tmp_path, frames, chunk, window, overlap):
    store = JobStore(settings(tmp_path))
    audio = song(frames)
    chunks = -(-frames // chunk)
    job = store.create("may", RATE, 2, frames, chunk, chunks)
    sep = FakeSeparator()
    # Đoạn đến lộn thứ tự; mỗi lần nhận một đoạn thì tách những gì đã đủ.
    order = list(range(chunks))[::-1] if chunks > 2 else list(range(chunks))
    for i in order:
        a, b = job.chunk_range(i)
        store.add_chunk(job.id, "may", i, audio[:, a:b], RATE)
        run_windows(store, job, sep, window, overlap)
        assert chunks_finalized(job) <= job.contiguous // chunk + (job.contiguous == frames)
    assert job.state == State.SEPARATING
    assert job.finalized == frames
    assert chunks_finalized(job) == chunks
    np.testing.assert_allclose(job.vocals, sep.separate(audio), atol=1e-6)


def test_crossfade_hides_window_edges(tmp_path):
    # Bộ tách giả thêm một mức lệch khác nhau mỗi cửa sổ: vùng chồng phải trượt dần, không nhảy.
    store = JobStore(settings(tmp_path))
    frames, window, overlap = 10000, 3000, 500
    job = store.create("may", RATE, 1, frames, frames, 1)
    store.add_chunk(job.id, "may", 0, np.zeros((1, frames), np.float32), RATE)
    k = 0
    while (w := next_window(job, window, overlap)) is not None:
        s, e = w
        apply_window(job, s, e, np.full((1, e - s), float(k), np.float32), overlap)
        k += 1
    jumps = np.abs(np.diff(job.vocals[0]))
    assert jumps.max() < 1.0 / overlap * 1.01  # bậc 1.0 chia đều trong vùng chồng


def test_create_validates(tmp_path):
    store = JobStore(settings(tmp_path, jobs_per_device_per_day=2))
    with pytest.raises(JobError) as e:
        store.create("may", 48000, 2, RATE, RATE, 1)
    assert e.value.status == 400
    with pytest.raises(JobError):
        store.create("may", RATE, 3, RATE, RATE, 1)
    with pytest.raises(JobError) as e:
        store.create("may", RATE, 2, RATE * 601, RATE * 20, 31)
    assert e.value.status == 413
    with pytest.raises(JobError):
        store.create("may", RATE, 2, RATE * 10, RATE * 3, 3)  # cần 4 đoạn
    store.create("may", RATE, 2, RATE, RATE, 1)
    store.create("may", RATE, 2, RATE, RATE, 1)
    with pytest.raises(JobError) as e:
        store.create("may", RATE, 2, RATE, RATE, 1)
    assert e.value.status == 429
    store.create("may-khac", RATE, 2, RATE, RATE, 1)  # giới hạn theo từng máy


def test_add_chunk_validates(tmp_path):
    store = JobStore(settings(tmp_path))
    job = store.create("may", RATE, 2, 1000, 400, 3)
    with pytest.raises(JobError) as e:
        store.add_chunk("khong-co", "may", 0, np.zeros((2, 400), np.float32), RATE)
    assert e.value.status == 404
    with pytest.raises(JobError) as e:
        store.add_chunk(job.id, "may-la", 0, np.zeros((2, 400), np.float32), RATE)
    assert e.value.status == 404  # máy khác không thấy job
    with pytest.raises(JobError):
        store.add_chunk(job.id, "may", 3, np.zeros((2, 400), np.float32), RATE)
    with pytest.raises(JobError):
        store.add_chunk(job.id, "may", 2, np.zeros((2, 400), np.float32), RATE)  # đoạn cuối 200
    with pytest.raises(JobError):
        store.add_chunk(job.id, "may", 0, np.zeros((1, 400), np.float32), RATE)
    store.add_chunk(job.id, "may", 2, np.zeros((2, 200), np.float32), RATE)
    assert job.contiguous == 0  # thiếu đoạn 0, 1
    store.add_chunk(job.id, "may", 0, np.zeros((2, 400), np.float32), RATE)
    assert job.contiguous == 400
    store.add_chunk(job.id, "may", 0, np.ones((2, 400), np.float32), RATE)  # gửi lại: bỏ qua
    assert job.pcm[0, 0] == 0.0
    store.add_chunk(job.id, "may", 1, np.zeros((2, 400), np.float32), RATE)
    assert job.contiguous == 1000
    assert job.state == State.SEPARATING
    assert job.upload_done_at is not None


def test_cleanup_expired_idle_and_orphans(tmp_path):
    s = settings(tmp_path, ttl_seconds=100, idle_seconds=10)
    store = JobStore(s)
    old = store.create("may", RATE, 1, 100, 100, 1)
    idle = store.create("may", RATE, 1, 100, 50, 2)
    fresh = store.create("may", RATE, 1, 100, 100, 1)
    now = time.time()
    old.created = now - 200
    idle.last_activity = now - 20
    (s.data_dir / "mo-coi").mkdir()
    assert store.cleanup(now) == 2
    assert set(store.jobs) == {fresh.id}
    assert not old.dir.exists() and not idle.dir.exists()
    assert not (s.data_dir / "mo-coi").exists()
    assert fresh.dir.exists()
    with pytest.raises(JobError):
        store.get(old.id, "may")
