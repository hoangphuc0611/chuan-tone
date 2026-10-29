# Server tách giọng ca sĩ (M6)

Kế hoạch: `docs/m6-plan.md`. Số đo: `docs/separation-results.md`.

> **Trọng số htdemucs chỉ dùng phát triển nội bộ.** Meta phát hành trọng số "chỉ cho mục đích khoa
> học" (issue facebookresearch/demucs#327). Không đưa server này ra internet / phát hành khi chưa
> chốt bộ tách có giấy phép thương mại (docs/m6-plan.md điểm 1).

## Cài đặt (Mac)

```sh
cd server
uv sync --extra demucs        # torch + demucs (không kéo lameenc / tqdm / dora / torchaudio)
uv run pytest                 # test dùng bộ tách giả, không cần torch
```

Lần đầu tách sẽ tải trọng số htdemucs (~84 MB) về `~/.cache/torch/hub/checkpoints/`.

## Đo bộ tách

```sh
afconvert -f WAVE -d LEI16@44100 bai.m4a /tmp/bai.wav
uv run python tools/bench_separate.py /tmp/bai.wav --seconds 240 --device mps --out /tmp/giong.wav
# đo SDR: trộn thêm một giọng khô đã biết
uv run python tools/bench_separate.py /tmp/bai.wav --vocal ../testdata/Audio/vocadito_6.wav
```
