"""Đo bộ tách trên một file WAV (docs/m6-plan.md B1).

  uv run --extra demucs python tools/bench_separate.py beat.wav --seconds 240 --device mps
  uv run --extra demucs python tools/bench_separate.py beat.wav --vocal giong.wav --out vocals.wav

--vocal: trộn thêm một giọng hát khô đã biết vào bài (lặp lại cho đủ dài) → đo SDR của stem giọng
tách ra so với giọng đã trộn: SDR = 10·log10(Σ s² / Σ (s − ŝ)²).
"""

from __future__ import annotations

import argparse
import time
import wave
from pathlib import Path

import numpy as np

from chuantone_server.separator import make_separator


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = (np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)).astype(np.float32)
    else:
        raise ValueError(f"WAV {width * 8}-bit chưa hỗ trợ")
    return x.reshape(-1, ch).T.copy(), rate


def write_wav(path: Path, audio: np.ndarray, rate: int) -> None:
    pcm = (np.clip(audio, -1.0, 1.0) * 32767.0).round().astype("<i2").T.tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


def fit(x: np.ndarray, n: int) -> np.ndarray:
    """Cắt hoặc lặp cho đủ n mẫu."""
    reps = -(-n // x.shape[1])
    return np.tile(x, (1, reps))[:, :n]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("--separator", default="htdemucs")
    ap.add_argument("--device", default=None, help="mps / cpu (mặc định: mps nếu có)")
    ap.add_argument("--seconds", type=float, default=None, help="cắt / lặp bài cho đủ thời lượng")
    ap.add_argument("--vocal", type=Path, default=None, help="giọng khô để trộn + đo SDR")
    ap.add_argument("--vocal-gain-db", type=float, default=0.0)
    ap.add_argument("--out", type=Path, default=None, help="ghi stem giọng (WAV 16-bit)")
    args = ap.parse_args()

    audio, rate = read_wav(args.input)
    if args.seconds:
        audio = fit(audio, int(args.seconds * rate))
    truth = None
    if args.vocal:
        v, vrate = read_wav(args.vocal)
        if vrate != rate:
            raise SystemExit(f"giọng {vrate} Hz khác bài {rate} Hz")
        v = fit(v.mean(axis=0, keepdims=True), audio.shape[1]) * 10 ** (args.vocal_gain_db / 20)
        truth = np.repeat(v, audio.shape[0], axis=0)
        audio = audio + truth

    kwargs = {"device": args.device} if args.separator == "htdemucs" and args.device else {}
    t0 = time.perf_counter()
    sep = make_separator(args.separator, **kwargs)
    t1 = time.perf_counter()
    if sep.sample_rate != rate:
        raise SystemExit(f"bộ tách cần {sep.sample_rate} Hz, file {rate} Hz")
    vocals = sep.separate(audio)
    t2 = time.perf_counter()

    dur = audio.shape[1] / rate
    print(f"bộ tách      {sep.name} ({getattr(sep, 'device', '-')})")
    print(f"bài          {dur:.1f} s, {audio.shape[0]} kênh, {rate} Hz")
    print(f"nạp model    {t1 - t0:.1f} s")
    print(f"tách         {t2 - t1:.1f} s  (×{dur / (t2 - t1):.1f} thời gian thực)")
    print(f"mức giọng    {20 * np.log10(np.sqrt(np.mean(vocals ** 2)) + 1e-12):.1f} dBFS RMS")
    if truth is not None:
        err = np.sum((truth - vocals) ** 2)
        print(f"SDR giọng    {10 * np.log10(np.sum(truth ** 2) / max(err, 1e-20)):.1f} dB")
    if args.out:
        write_wav(args.out, vocals, rate)
        print(f"ghi          {args.out}")


if __name__ == "__main__":
    main()
