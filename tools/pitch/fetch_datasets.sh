#!/usr/bin/env bash
# Tải bộ dữ liệu test cho dò cao độ / chỉnh giọng vào testdata/ (KHÔNG commit, KHÔNG ship).
#
#   tools/pitch/fetch_datasets.sh              # vocadito (58,5 MB, CC BY 4.0)
#   tools/pitch/fetch_datasets.sh --vocalset   # thêm VocalSet 1.2 (8,1 GB, CC BY 4.0)
#
# Nguồn và cách ghi công: docs/licenses.md, mục "Dữ liệu test".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/testdata"
mkdir -p "$DEST"

sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }

fetch() {  # url file sha256
  local url="$1" file="$2" want="$3"
  if [[ -f "$DEST/$file" && "$(sha256 "$DEST/$file")" == "$want" ]]; then
    echo "Đã có $file"
    return
  fi
  echo "Tải $file…"
  curl -fL --retry 3 -o "$DEST/$file.part" "$url"
  local got
  got="$(sha256 "$DEST/$file.part")"
  if [[ "$got" != "$want" ]]; then
    echo "Sai SHA-256 cho $file: $got" >&2
    rm -f "$DEST/$file.part"
    exit 1
  fi
  mv "$DEST/$file.part" "$DEST/$file"
}

# vocadito v3 — R. Bittner, K. Pasalo, J. J. Bosch, G. Meseguer-Brocal, D. Rubinstein,
# zenodo.org/records/5578807
fetch "https://zenodo.org/api/records/5578807/files/vocadito.zip/content" vocadito.zip \
  e0d6b99d3f9c594afe5ae5c4d7bdacebe569e53b809e90b89d1c771c4f9990e3
if [[ ! -d "$DEST/Audio" ]]; then
  (cd "$DEST" && unzip -q -o vocadito.zip -x '__MACOSX/*')
fi

if [[ "${1:-}" == "--vocalset" ]]; then
  # VocalSet 1.2 — J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo, zenodo.org/records/1442513
  echo "VocalSet ~8,1 GB. Tải thủ công nếu cần: https://zenodo.org/records/1442513" >&2
  echo "Giải nén vào $DEST/VocalSet (chưa tự động hoá vì dung lượng lớn)." >&2
fi

echo "Xong: $DEST"
