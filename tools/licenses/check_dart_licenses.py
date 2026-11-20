#!/usr/bin/env python3
"""Kiểm license mọi gói Dart của app (M9 B6, CLAUDE.md mục 0.2 và 8).

Đọc `app/pubspec.lock`, tìm file LICENSE của từng gói trong pub cache, nhận dạng loại license và:
  - báo lỗi nếu ngoài danh sách cho phép (MIT / BSD / Apache-2.0 / Zlib / ISC / public domain),
    trừ các ngoại lệ đã ghi trong docs/licenses.md (bảng NGOAI_LE dưới đây, kèm lý do);
  - báo lỗi nếu gói phụ thuộc trực tiếp (dependency: direct main) chưa có tên trong docs/licenses.md.

Chạy sau `flutter pub get`:  python3 tools/licenses/check_dart_licenses.py
Chỉ dùng thư viện chuẩn của Python.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CHO_PHEP = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "Zlib", "ISC", "Public domain"}

# Gói ngoài danh sách cho phép nhưng không vào bản Android / iOS (đã ghi ở docs/licenses.md).
NGOAI_LE = {
    "dbus": "MPL-2.0 — chỉ file_picker_linux (bản Linux desktop) kéo vào, không build vào app Android/iOS",
}


def doc_lock(path: Path) -> list[dict]:
    """Đọc pubspec.lock (YAML đơn giản, không cần thư viện): tên, phiên bản, nguồn, loại phụ thuộc."""
    goi: list[dict] = []
    hien_tai: dict | None = None
    trong_packages = False
    for dong in path.read_text(encoding="utf-8").splitlines():
        if dong.startswith("packages:"):
            trong_packages = True
            continue
        if trong_packages and dong and not dong.startswith(" "):
            break  # hết mục packages
        m = re.match(r"^  ([A-Za-z0-9_]+):\s*$", dong)
        if trong_packages and m:
            hien_tai = {"name": m.group(1)}
            goi.append(hien_tai)
            continue
        if hien_tai is None:
            continue
        m = re.match(r"^    (dependency|source|version):\s*\"?([^\"]*)\"?\s*$", dong)
        if m:
            hien_tai[m.group(1)] = m.group(2)
    return goi


def thu_muc_pub_cache() -> Path:
    env = os.environ.get("PUB_CACHE")
    return Path(env) if env else Path.home() / ".pub-cache"


def nhan_dang(text: str) -> str:
    t = re.sub(r"\s+", " ", text)
    # MPL trước: văn bản MPL-2.0 có nhắc tới GPL / LGPL (mục "Secondary License").
    if "Mozilla Public License" in t:
        return "MPL-2.0"
    if "GNU LESSER GENERAL PUBLIC" in t.upper():
        return "LGPL"
    if "GNU GENERAL PUBLIC" in t.upper() or "GNU AFFERO" in t.upper():
        return "GPL"
    if "Apache License" in t and "2.0" in t:
        return "Apache-2.0"
    if "Redistribution and use in source and binary forms" in t:
        return "BSD-3-Clause" if "Neither the name" in t or "neither the name" in t else "BSD-2-Clause"
    if "Permission is hereby granted, free of charge" in t:
        return "MIT"
    if "Permission to use, copy, modify, and/or distribute this software for any purpose" in t:
        return "ISC"
    if "This software is provided 'as-is'" in t or "zlib License" in t:
        return "Zlib"
    if "public domain" in t.lower() and len(t) < 3000:
        return "Public domain"
    return "?"


def license_cua(goi: dict, cache: Path) -> tuple[str, Path | None]:
    thu_muc = cache / "hosted" / "pub.dev" / f"{goi['name']}-{goi.get('version', '')}"
    if not thu_muc.is_dir():
        return "không thấy trong pub cache", None
    for ten in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING", "LICENSE-MIT"):
        f = thu_muc / ten
        if f.is_file():
            return nhan_dang(f.read_text(encoding="utf-8", errors="replace")), f
    return "không có file LICENSE", None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lock", default=str(ROOT / "app" / "pubspec.lock"))
    ap.add_argument("--doc", default=str(ROOT / "docs" / "licenses.md"))
    args = ap.parse_args()

    cache = thu_muc_pub_cache()
    doc = Path(args.doc).read_text(encoding="utf-8")
    loi: list[str] = []
    dem: dict[str, int] = {}
    for goi in doc_lock(Path(args.lock)):
        nguon = goi.get("source", "")
        ten = goi["name"]
        if nguon in ("sdk", "path"):
            continue  # Flutter SDK (BSD-3) / gói của dự án
        loai, _ = license_cua(goi, cache)
        dem[loai] = dem.get(loai, 0) + 1
        if loai not in CHO_PHEP:
            if ten in NGOAI_LE:
                print(f"  ngoại lệ  {ten} {goi.get('version')}: {NGOAI_LE[ten]}")
            else:
                loi.append(f"{ten} {goi.get('version')}: license {loai} ngoài danh sách cho phép")
        if goi.get("dependency") == "direct main" and not re.search(
            rf"(^|[^A-Za-z0-9_]){re.escape(ten)}([^A-Za-z0-9_]|$)", doc
        ):
            loi.append(f"{ten}: phụ thuộc trực tiếp chưa ghi trong docs/licenses.md")
    print("Số gói theo license: " + ", ".join(f"{k} {v}" for k, v in sorted(dem.items())))
    if loi:
        print("LỖI:")
        for l in loi:
            print(f"  - {l}")
        return 1
    print("OK: mọi gói Dart trong license cho phép (hoặc ngoại lệ đã ghi).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
