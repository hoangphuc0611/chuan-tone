"""Lớp "bộ tách" thay được (docs/m6-plan.md điểm 1): server chỉ biết giao diện Separator.

- "fake": bộ tách giả cho test / CI (không cần torch).
- "htdemucs": Demucs v4 của Meta — CHỈ PHÁT TRIỂN NỘI BỘ, trọng số "chỉ cho mục đích khoa học".
"""

from __future__ import annotations

import contextlib
import importlib.util
import sys
import types
from pathlib import Path
from typing import Callable, Protocol

import numpy as np

Progress = Callable[[float], None]


class Separator(Protocol):
    name: str
    sample_rate: int  # tần số model cần; server đổi tần số trước nếu khác

    def separate(self, audio: np.ndarray, progress: Progress | None = None) -> np.ndarray:
        """audio: float32 [kênh, mẫu] (1 hoặc 2 kênh) ở sample_rate → stem giọng cùng kích thước."""
        ...


class FakeSeparator:
    """Coi kênh giữa là giọng: (L + R) / 2 · gain, cùng kích thước đầu vào. Chỉ để test đường đi
    của dữ liệu (đúng hình dạng, đúng thứ tự, đúng tiến độ), không phải tách thật."""

    name = "fake"
    sample_rate = 44100

    def __init__(self, gain: float = 0.5) -> None:
        self.gain = gain

    def separate(self, audio: np.ndarray, progress: Progress | None = None) -> np.ndarray:
        _check(audio)
        mid = audio.mean(axis=0, keepdims=True) * self.gain
        if progress:
            progress(1.0)
        return np.repeat(mid, audio.shape[0], axis=0).astype(np.float32)


class DemucsSeparator:
    """htdemucs (Rouard, Massa, Défossez, "Hybrid Transformers for Music Source Separation",
    ICASSP 2023) qua API công khai của gói demucs (get_model / apply_model)."""

    name = "htdemucs"
    sample_rate = 44100

    def __init__(
        self,
        device: str | None = None,
        shifts: int = 1,
        overlap: float = 0.25,
        half: bool = True,
    ) -> None:
        _use_stubs()
        import torch
        from demucs.pretrained import get_model

        if device is None:
            device = ("cuda" if torch.cuda.is_available()
                      else "mps" if torch.backends.mps.is_available() else "cpu")
        self.device = device
        self.shifts = shifts
        self.overlap = overlap
        # Tính 16-bit trên GPU (autocast: STFT vẫn 32-bit): trên M1 nhanh gấp ~2 lần (bớt thiếu
        # RAM), lệch so với 32-bit −57 dB (đo ở B1, docs/separation-results.md).
        self.half = half and device in ("mps", "cuda")
        self._torch = torch
        self._model = get_model("htdemucs")
        self._model.eval()
        self.sample_rate = int(self._model.samplerate)
        self._vocals = list(self._model.sources).index("vocals")

    def separate(self, audio: np.ndarray, progress: Progress | None = None) -> np.ndarray:
        from demucs.apply import apply_model

        _check(audio)
        torch = self._torch
        channels = audio.shape[0]
        wav = torch.from_numpy(np.ascontiguousarray(audio, dtype=np.float32))
        if channels == 1:
            wav = wav.repeat(2, 1)  # model cần stereo
        # Chuẩn hoá theo kênh giữa như khi huấn luyện, tách, rồi trả về thang gốc.
        ref = wav.mean(0)
        mean, std = ref.mean(), ref.std().clamp_min(1e-8)
        autocast = (
            torch.autocast(self.device, dtype=torch.float16) if self.half
            else contextlib.nullcontext()
        )
        with torch.no_grad(), autocast:
            sources = apply_model(
                self._model,
                ((wav - mean) / std)[None],
                device=self.device,
                shifts=self.shifts,
                split=True,
                overlap=self.overlap,
                progress=False,
                num_workers=0,
            )[0]
        vocals = (sources[self._vocals].float() * std + mean).cpu().numpy()
        if channels == 1:
            vocals = vocals.mean(axis=0, keepdims=True)
        if progress:
            progress(1.0)
        return vocals.astype(np.float32)


def make_separator(name: str, **kwargs) -> Separator:
    if name == "fake":
        return FakeSeparator(**kwargs)
    if name == "htdemucs":
        return DemucsSeparator(**kwargs)
    raise ValueError(f"bộ tách không có: {name}")


def _check(audio: np.ndarray) -> None:
    if audio.ndim != 2 or audio.shape[0] not in (1, 2):
        raise ValueError(f"cần mảng [1 hoặc 2 kênh, mẫu], nhận {audio.shape}")


def _use_stubs() -> None:
    """Cho demucs import được mà không kéo phụ thuộc thừa (pyproject.toml):
    - tqdm / dora.log / omegaconf rỗng — đặt CUỐI sys.path: gói thật nếu có vẫn được ưu tiên;
    - openunmix: demucs chỉ import openunmix.filtering (wiener, chỉ cần torch), nhưng
      openunmix/__init__.py kéo torchaudio → đăng ký gói openunmix mà không chạy __init__.py."""
    stubs = str(Path(__file__).with_name("_stubs"))
    if stubs not in sys.path:
        sys.path.append(stubs)
    if "openunmix" not in sys.modules:
        spec = importlib.util.find_spec("openunmix")
        if spec is not None and spec.submodule_search_locations:
            pkg = types.ModuleType("openunmix")
            pkg.__path__ = list(spec.submodule_search_locations)
            sys.modules["openunmix"] = pkg
