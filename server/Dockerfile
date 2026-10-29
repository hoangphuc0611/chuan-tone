# Server tách giọng cho máy chủ GPU (M9). Build từ thư mục gốc repo:
#   docker build -f server/Dockerfile -t chuantone-server .
#   docker run --gpus all -p 8000:8000 chuantone-server
# Trọng số htdemucs CHỈ PHÁT TRIỂN NỘI BỘ (docs/m6-plan.md điểm 1): không chạy image này cho người
# dùng thật khi chưa chốt bộ tách có giấy phép thương mại.

FROM ubuntu:24.04 AS tcflac
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build clang ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY engine engine
RUN cd engine && CC=clang CXX=clang++ cmake --preset host-release -DTC_BUILD_TESTS=OFF -DTC_BUILD_BENCH=OFF \
    && cmake --build --preset host-release --target tcflac

FROM python:3.13-slim
COPY --from=ghcr.io/astral-sh/uv:0.13.2 /uv /usr/local/bin/uv
WORKDIR /app
COPY server/pyproject.toml server/uv.lock server/README.md ./
RUN uv sync --frozen --extra demucs --no-install-project
COPY server/src src
RUN uv sync --frozen --extra demucs
COPY --from=tcflac /src/engine/build/host-release/tools/tcflac /usr/local/bin/tcflac
ENV CHUANTONE_TCFLAC=/usr/local/bin/tcflac CHUANTONE_DATA_DIR=/data
EXPOSE 8000
CMD ["uv", "run", "--frozen", "chuantone-server", "--port", "8000"]
