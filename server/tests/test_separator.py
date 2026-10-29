import importlib
import sys

import numpy as np
import pytest

from chuantone_server import separator
from chuantone_server.separator import FakeSeparator, make_separator


def test_fake_separator_shape_and_progress():
    sep = make_separator("fake")
    audio = np.stack([np.linspace(-1, 1, 1000), np.linspace(1, -1, 1000)]).astype(np.float32)
    seen = []
    vocals = sep.separate(audio, progress=seen.append)
    assert vocals.shape == audio.shape
    assert vocals.dtype == np.float32
    np.testing.assert_allclose(vocals[0], audio.mean(axis=0) * 0.5, atol=1e-7)
    assert seen == [1.0]


def test_fake_separator_mono():
    vocals = FakeSeparator(gain=1.0).separate(np.ones((1, 10), dtype=np.float32))
    assert vocals.shape == (1, 10)
    np.testing.assert_allclose(vocals, 1.0)


@pytest.mark.parametrize("shape", [(10,), (3, 10), (0, 10)])
def test_rejects_bad_shape(shape):
    with pytest.raises(ValueError):
        FakeSeparator().separate(np.zeros(shape, dtype=np.float32))


def test_unknown_separator():
    with pytest.raises(ValueError):
        make_separator("khong-co")


def test_stubs_are_importable_and_inert(monkeypatch):
    # Mô-đun rỗng thay tqdm / dora.log / omegaconf: chỉ dùng khi gói thật không có.
    for name in ["tqdm", "dora", "dora.log", "omegaconf"]:
        monkeypatch.delitem(sys.modules, name, raising=False)
    monkeypatch.setattr(sys, "path", [p for p in sys.path])
    separator._use_stubs()
    tqdm = importlib.import_module("tqdm")
    if getattr(tqdm, "__file__", "").find("_stubs") < 0:
        pytest.skip("tqdm thật đang được cài")
    with tqdm.tqdm(total=10) as bar:
        bar.update(3)
    assert list(tqdm.tqdm([1, 2])) == [1, 2]
    dora_log = importlib.import_module("dora.log")
    assert dora_log.bold("x") == "x"
    with pytest.raises(RuntimeError):
        dora_log.fatal("lỗi")
