# Server tách giọng ca sĩ (M6)

Kế hoạch: `docs/m6-plan.md`. Giao thức: `docs/adr/006-vocal-separation.md`. Số đo:
`docs/separation-results.md`.

> **Trọng số htdemucs chỉ dùng phát triển nội bộ.** Meta phát hành trọng số "chỉ cho mục đích khoa
> học" (issue facebookresearch/demucs#327). Không đưa server này ra internet / cho người dùng thật
> khi chưa chốt bộ tách có giấy phép thương mại (docs/m6-plan.md điểm 1).

## Chạy trên Mac

```sh
cd engine && cmake --preset host-release && cmake --build --preset host-release --target tcflac
cd ../server
uv sync --extra demucs        # torch + demucs (không kéo lameenc / tqdm / dora / torchaudio)
uv run chuantone-server       # in "nhập vào app: http://192.168.x.x:8000"
```

- Lần đầu tải trọng số htdemucs (~84 MB) về `~/.cache/torch/hub/checkpoints/`.
- File tạm ở `~/.chuantone-server/` (đổi bằng `CHUANTONE_DATA_DIR`), tự xoá sau khi app tải xong
  hoặc sau 24 giờ.
- `CHUANTONE_API_KEY=...` để bắt buộc header `Authorization: Bearer ...`.
- iPhone và Mac cần chung mạng Wi-Fi. Khi đo nghiệm thu nên đóng bớt ứng dụng nặng (Mac 8 GB).

## Test, đo

```sh
uv run pytest                 # bộ tách giả, cần tcflac (không cần torch)
# giả lập app gửi một bài (server đang chạy):
uv run python tools/client_sim.py http://127.0.0.1:8000 bai.m4a --mbps 20
# đo riêng bộ tách:
afconvert -f WAVE -d LEI16@44100 bai.m4a /tmp/bai.wav
uv run python tools/bench_separate.py /tmp/bai.wav --seconds 240 --device mps
```
