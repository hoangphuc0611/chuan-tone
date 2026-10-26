#!/usr/bin/env bash
# Tải mốc so sánh Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.4 (cả hai MIT) vào
# testdata/ref/ (KHÔNG commit, KHÔNG ship). beatlab tự build kèm khi thấy thư mục này:
#
#   tools/stretch/fetch_signalsmith.sh
#   cmake --preset host-release && cmake --build engine/build/rel --target beatlab
#
# Chỉ dùng như hộp đen qua API công khai (README) để so A/B với PitchShifter của ToneCore —
# không đọc / chép mã bên trong (nguyên tắc clean-room, CLAUDE.md mục 0). License: docs/licenses.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/testdata/ref"
mkdir -p "$DEST"

fetch() {  # tên  repo  tag  commit
  local name="$1" repo="$2" tag="$3" want="$4"
  if [[ -d "$DEST/$name/.git" ]] && [[ "$(git -C "$DEST/$name" rev-parse HEAD)" == "$want" ]]; then
    echo "Đã có $name $tag"
    return
  fi
  rm -rf "$DEST/$name"
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "$repo" "$DEST/$name"
  local got
  got="$(git -C "$DEST/$name" rev-parse HEAD)"
  if [[ "$got" != "$want" ]]; then
    echo "Sai commit cho $name $tag: $got (cần $want)" >&2
    exit 1
  fi
  echo "Đã tải $name $tag"
}

fetch signalsmith-stretch https://github.com/Signalsmith-Audio/signalsmith-stretch.git 1.4.0 \
  a670068d9aeb64913331d5cc29337b19a457a7df
fetch signalsmith-linear https://github.com/Signalsmith-Audio/linear.git 0.6.4 \
  de55e6a50ffcf6f8f43f649692d94691c7025151
