#!/usr/bin/env bash
# Tải GuitarSet 1.1.0 (CC BY 4.0) vào testdata/GuitarSet/ (KHÔNG commit, KHÔNG ship) rồi tạo
# testdata/GuitarSet/guitarset.csv để chấm bằng keytest:
#
#   tools/keytest/fetch_guitarset.sh
#   python3 tools/keytest/keytest.py run --csv testdata/GuitarSet/guitarset.csv
#
# Chỉ tải nhãn (39 MB) + audio micro mono (657 MB). Q. Xi, R. M. Bittner, J. Pauwels, X. Ye,
# J. P. Bello, "GuitarSet: A Dataset for Guitar Transcription", ISMIR 2018 — zenodo.org/records/3371780.
# Ghi công và license: docs/licenses.md, mục "Dữ liệu test".
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/testdata/GuitarSet"
mkdir -p "$DEST"

md5() { if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }

fetch() {  # file md5 (checksum Zenodo công bố)
  local file="$1" want="$2"
  if [[ -f "$DEST/$file" && "$(md5 "$DEST/$file")" == "$want" ]]; then
    echo "Đã có $file"
    return
  fi
  echo "Tải $file…"
  curl -fL --retry 3 -o "$DEST/$file.part" "https://zenodo.org/api/records/3371780/files/$file/content"
  local got
  got="$(md5 "$DEST/$file.part")"
  if [[ "$got" != "$want" ]]; then
    echo "Sai MD5 cho $file: $got" >&2
    rm -f "$DEST/$file.part"
    exit 1
  fi
  mv "$DEST/$file.part" "$DEST/$file"
}

count() { find "$DEST/$1" -name "*.$2" 2>/dev/null | wc -l | tr -d ' '; }

# Đã giải nén đủ 360 bài thì không tải lại; giải nén xong thì xoá zip (đỡ ~700 MB, nhất là cache CI).
extract() {  # file md5 thư_mục đuôi
  local file="$1" want="$2" dir="$3" ext="$4"
  if [[ "$(count "$dir" "$ext")" == "360" ]]; then
    echo "Đã có $dir/ (360 file)"
    return
  fi
  fetch "$file" "$want"
  rm -rf "${DEST:?}/$dir" && mkdir -p "$DEST/$dir"
  (cd "$DEST/$dir" && unzip -q -o "../$file")
  rm -f "$DEST/$file"
}

extract annotation.zip b39b78e63d3446f2e54ddb7a54df9b10 annotation jams
extract audio_mono-mic.zip 275966d6610ac34999b58426beb119c3 audio_mono-mic wav

python3 "$ROOT/tools/keytest/guitarset_csv.py" "$DEST"
echo "Xong: $DEST/guitarset.csv"
