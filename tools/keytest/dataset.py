"""Đọc và kiểm tra file CSV nhãn tone của bộ dữ liệu (dev / test).

Định dạng (UTF-8, có thể có BOM của Excel; dấu phân cách `,`, `;` hoặc tab — tự nhận theo dòng
tiêu đề). Xem hướng dẫn gán nhãn đầy đủ ở tools/keytest/README.md.

    file,key,mode,modulation,bpm,genre,notes
    bolero/duyen_phan.mp3,A,minor,"1:35:A:major;2:40.5:A:minor",68,bolero,
    ballad/abc.m4a,C#,major,,76,ballad,

- file: đường dẫn tương đối so với thư mục chứa CSV.
- key, mode: tone lúc vào bài (sau intro). `key` có thể viết gọn kiểu "Am" (khi đó `mode` được
  bỏ trống hoặc phải khớp).
- modulation (không bắt buộc): các mốc `thời_điểm:chủ_âm:mode` (hoặc `thời_điểm:Am`) cách nhau
  bằng `;`; thời điểm viết theo giây (`95.5`) hoặc phút:giây (`1:35.5`).
- bpm, genre, notes: không bắt buộc. Cột khác (vd. artist) được bỏ qua.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from mirex import MAJOR, MINOR, Key, Segment, parse_mode, parse_tonic

REQUIRED = ("file", "key")
MODES = (MAJOR, MINOR)


@dataclass
class Song:
    line: int
    file: str
    path: Path
    key: Key
    modulations: list = field(default_factory=list)  # [(giây, Key)]
    bpm: Optional[float] = None
    genre: str = ""
    notes: str = ""

    @property
    def has_modulation(self) -> bool:
        return bool(self.modulations)

    def reference_timeline(self, duration: float) -> list:
        """Timeline đáp án [0, duration). Mốc chuyển tone sau `duration` bị bỏ."""
        points = [(0.0, self.key)] + [(t, k) for t, k in self.modulations if t < duration]
        segs = []
        for i, (t, k) in enumerate(points):
            end = points[i + 1][0] if i + 1 < len(points) else duration
            if end > t:
                segs.append(Segment(t, end, k))
        return segs


@dataclass
class Dataset:
    csv_path: Path
    songs: list
    errors: list
    warnings: list

    @property
    def ok(self) -> bool:
        return not self.errors


def parse_time(text: str) -> float:
    """'95' / '95.5' / '1:35' / '1:35.5' / '1:02:03' → giây."""
    parts = text.strip().split(":")
    if not parts or len(parts) > 3 or any(p.strip() == "" for p in parts):
        raise ValueError(f"thời điểm không hợp lệ: {text!r}")
    total = 0.0
    for i, p in enumerate(parts):
        v = float(p)
        if v < 0 or (i > 0 and v >= 60):
            raise ValueError(f"thời điểm không hợp lệ: {text!r}")
        total = total * 60 + v
    return total


def parse_short_key(text: str) -> Key:
    """'Am' → La thứ; 'C#' → Đô thăng trưởng; 'Bbm' → Si giáng thứ."""
    s = text.strip()
    if len(s) >= 2 and s.endswith("m"):
        return Key(parse_tonic(s[:-1]), MINOR)
    return Key(parse_tonic(s), MAJOR)


def parse_key_columns(key_text: str, mode_text: str) -> Key:
    key_text, mode_text = key_text.strip(), mode_text.strip()
    if not mode_text:
        return parse_short_key(key_text)
    mode = parse_mode(mode_text)
    try:
        return Key(parse_tonic(key_text), mode)
    except ValueError:
        k = parse_short_key(key_text)  # "Am" + "minor" vẫn chấp nhận nếu khớp
        if k.mode != mode:
            raise ValueError(f"cột key {key_text!r} mâu thuẫn với mode {mode_text!r}")
        return k


def parse_modulations(text: str) -> list:
    """'1:35:A:major;2:40:Am' → [(95.0, A trưởng), (160.0, La thứ)]."""
    out = []
    for item in text.split(";"):
        item = item.strip()
        if not item:
            continue
        parts = [p.strip() for p in item.split(":")]
        if len(parts) < 2:
            raise ValueError(f"mốc chuyển tone không hợp lệ: {item!r} (dạng 1:35:A:major)")
        try:
            mode = parse_mode(parts[-1])
            if len(parts) < 3:
                raise ValueError
            key = Key(parse_tonic(parts[-2]), mode)
            time_text = ":".join(parts[:-2])
        except ValueError:
            key = parse_short_key(parts[-1])
            time_text = ":".join(parts[:-1])
        out.append((parse_time(time_text), key))
    return out


def _sniff_delimiter(header: str) -> str:
    counts = {d: header.count(d) for d in (",", ";", "\t")}
    return max(counts, key=lambda d: counts[d]) if any(counts.values()) else ","


def load(csv_path: Path, check_files: bool = True) -> Dataset:
    csv_path = Path(csv_path)
    errors: list = []
    warnings: list = []
    songs: list = []
    text = csv_path.read_text(encoding="utf-8-sig")
    lines = text.splitlines()
    if not lines:
        return Dataset(csv_path, [], [f"{csv_path}: file rỗng"], [])
    reader = csv.DictReader(lines, delimiter=_sniff_delimiter(lines[0]))
    fields = [f.strip().lower() for f in (reader.fieldnames or [])]
    reader.fieldnames = fields
    missing = [c for c in REQUIRED if c not in fields]
    if missing:
        return Dataset(csv_path, [], [f"thiếu cột: {', '.join(missing)}"], [])

    seen: dict = {}
    base = csv_path.parent
    for row in reader:
        line = reader.line_num
        get = lambda c: (row.get(c) or "").strip()  # noqa: E731
        if not any(get(c) for c in fields):
            continue  # dòng trống
        prefix = f"dòng {line}"
        file = get("file")
        if not file:
            errors.append(f"{prefix}: thiếu tên file")
            continue
        try:
            key = parse_key_columns(get("key"), get("mode"))
        except ValueError as e:
            errors.append(f"{prefix} ({file}): {e}")
            continue
        try:
            mods = parse_modulations(get("modulation"))
        except ValueError as e:
            errors.append(f"{prefix} ({file}): {e}")
            continue
        prev_t, prev_k, bad = 0.0, key, False
        for t, k in mods:
            if t <= prev_t:
                errors.append(f"{prefix} ({file}): mốc chuyển tone phải tăng dần và > 0 "
                              f"(gặp {t:g} s sau {prev_t:g} s)")
                bad = True
            if k == prev_k:
                errors.append(f"{prefix} ({file}): chuyển sang {k.name} nhưng đang ở {k.name}")
                bad = True
            prev_t, prev_k = t, k
        if bad:
            continue
        bpm = None
        if get("bpm"):
            try:
                bpm = float(get("bpm").replace(",", "."))
            except ValueError:
                bpm = -1.0
            if not 30.0 <= bpm <= 300.0:
                errors.append(f"{prefix} ({file}): bpm {get('bpm')!r} ngoài khoảng 30–300")
                continue
        path = (base / file).resolve()
        if path in seen:
            errors.append(f"{prefix} ({file}): trùng với dòng {seen[path]}")
            continue
        seen[path] = line
        if check_files and not path.is_file():
            errors.append(f"{prefix}: không thấy file {path}")
            continue
        songs.append(Song(line, file, path, key, mods, bpm, get("genre"), get("notes")))
    if not songs and not errors:
        warnings.append("không có bài nào")
    return Dataset(csv_path, songs, errors, warnings)
