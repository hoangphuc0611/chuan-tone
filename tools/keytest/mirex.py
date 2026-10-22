"""Tone (key) và cách chấm điểm MIREX Audio Key Detection.

Bảng điểm (giống `mir_eval.key.weighted_score`, C. Raffel et al., "mir_eval: A Transparent
Implementation of Common MIR Metrics", ISMIR 2014):

    Đúng cả chủ âm và mode                                1,0
    Cùng mode, tone đoán CAO HƠN đáp án một quãng 5       0,5   (vd. đáp án C, đoán G)
    Tương đối trưởng/thứ (cùng hoá biểu)                  0,3   (C ↔ Am)
    Song song (cùng chủ âm, khác mode)                    0,2   (Am ↔ A)
    Sai                                                   0

`descending_fifths=True` cho thêm 0,5 khi đoán THẤP hơn một quãng 5 (vd. đáp án C, đoán F) — chỉ
để báo cáo tham khảo; chỉ tiêu M3 dùng bảng chuẩn ở trên (docs/m3-plan.md, điểm 3).

Bài có chuyển tone: điểm = trung bình điểm MIREX theo thời lượng trên timeline.
Chỉ dùng thư viện chuẩn Python (chạy được với Python 3.9 của macOS).
"""

from __future__ import annotations

import bisect
from dataclasses import dataclass
from typing import Optional, Sequence

MAJOR = "major"
MINOR = "minor"

_LETTER = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
# Tên hiển thị: dùng dấu thăng/giáng như cách gọi tone thường gặp ở Việt Nam (Eb, Bb, F#, C#m…).
_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
_MODE_ALIASES = {
    "major": MAJOR, "maj": MAJOR, "trưởng": MAJOR, "truong": MAJOR,
    "minor": MINOR, "min": MINOR, "thứ": MINOR, "thu": MINOR,
}

# Loại quan hệ giữa tone đoán và đáp án (dùng cho báo cáo).
CORRECT = "correct"
FIFTH = "fifth"            # đoán cao hơn quãng 5
FIFTH_DOWN = "fifth_down"  # đoán thấp hơn quãng 5 (chỉ được điểm khi descending_fifths)
RELATIVE = "relative"
PARALLEL = "parallel"
OTHER = "other"
MISSING = "missing"        # không có kết quả (keylab lỗi hoặc chưa dò được)

RELATION_LABELS = {
    CORRECT: "Đúng",
    FIFTH: "Lệch quãng 5 (lên)",
    FIFTH_DOWN: "Lệch quãng 5 (xuống)",
    RELATIVE: "Tương đối trưởng/thứ",
    PARALLEL: "Song song",
    OTHER: "Sai",
    MISSING: "Không có kết quả",
}


def parse_tonic(text: str) -> int:
    """'C', 'c#', 'Db', 'F♯', 'Bb' → 0..11. Sai định dạng → ValueError."""
    s = text.strip().replace("♯", "#").replace("♭", "b")
    if not s or s[0].lower() not in _LETTER:
        raise ValueError(f"chủ âm không hợp lệ: {text!r}")
    pc = _LETTER[s[0].lower()]
    for acc in s[1:]:
        if acc == "#":
            pc += 1
        elif acc == "b":
            pc -= 1
        else:
            raise ValueError(f"chủ âm không hợp lệ: {text!r}")
    if len(s) > 2:
        raise ValueError(f"chủ âm không hợp lệ: {text!r}")
    return pc % 12


def parse_mode(text: str) -> str:
    m = _MODE_ALIASES.get(text.strip().lower())
    if m is None:
        raise ValueError(f"mode không hợp lệ: {text!r} (dùng major/minor)")
    return m


@dataclass(frozen=True)
class Key:
    tonic: int  # 0 = Đô (C) … 11 = Si (B)
    mode: str   # MAJOR | MINOR

    @property
    def name(self) -> str:
        """'Am', 'C#', 'Eb'…"""
        return _NAMES[self.tonic % 12] + ("m" if self.mode == MINOR else "")

    @property
    def mir_eval_name(self) -> str:
        """Định dạng của mir_eval: 'A minor'."""
        return f"{_NAMES[self.tonic % 12]} {self.mode}"

    @staticmethod
    def parse(tonic: str, mode: str) -> "Key":
        return Key(parse_tonic(tonic), parse_mode(mode))


def relation(ref: Key, est: Optional[Key]) -> str:
    if est is None:
        return MISSING
    d = (est.tonic - ref.tonic) % 12
    if est.mode == ref.mode:
        if d == 0:
            return CORRECT
        if d == 7:
            return FIFTH
        if d == 5:
            return FIFTH_DOWN
        return OTHER
    if ref.mode == MAJOR and d == 9:  # C → Am
        return RELATIVE
    if ref.mode == MINOR and d == 3:  # Am → C
        return RELATIVE
    if d == 0:
        return PARALLEL
    return OTHER


_SCORES = {CORRECT: 1.0, FIFTH: 0.5, RELATIVE: 0.3, PARALLEL: 0.2}


def score(ref: Key, est: Optional[Key], descending_fifths: bool = False) -> float:
    rel = relation(ref, est)
    if rel == FIFTH_DOWN:
        return 0.5 if descending_fifths else 0.0
    return _SCORES.get(rel, 0.0)


@dataclass(frozen=True)
class Segment:
    start: float
    end: float
    key: Key


def key_at(segments: Sequence[Segment], t: float) -> Optional[Key]:
    """Tone tại thời điểm t; None nếu t nằm ngoài mọi đoạn. `segments` sắp theo start."""
    starts = [s.start for s in segments]
    i = bisect.bisect_right(starts, t) - 1
    if i >= 0 and segments[i].start <= t < segments[i].end:
        return segments[i].key
    return None


def timeline_score(ref: Sequence[Segment], est: Sequence[Segment],
                   descending_fifths: bool = False) -> float:
    """Điểm MIREX trung bình theo thời lượng trên toàn bộ timeline của đáp án.

    Chia trục thời gian theo mọi biên của cả hai timeline; mỗi khoảng nhỏ có một tone đáp án và
    một tone đoán (hoặc không có → 0 điểm).
    """
    ref = sorted(ref, key=lambda s: s.start)
    est = sorted(est, key=lambda s: s.start)
    if not ref:
        raise ValueError("timeline đáp án rỗng")
    lo, hi = ref[0].start, ref[-1].end
    cuts = {lo, hi}
    for s in list(ref) + list(est):
        for t in (s.start, s.end):
            if lo < t < hi:
                cuts.add(t)
    edges = sorted(cuts)
    total = weighted = 0.0
    for a, b in zip(edges, edges[1:]):
        mid = 0.5 * (a + b)
        r = key_at(ref, mid)
        if r is None:
            continue  # khe trống trong đáp án: không tính
        total += b - a
        weighted += (b - a) * score(r, key_at(est, mid), descending_fifths)
    return weighted / total if total > 0 else 0.0


def main_key(segments: Sequence[Segment]) -> Optional[Key]:
    """Tone chiếm nhiều thời lượng nhất (bằng nhau thì lấy tone xuất hiện trước)."""
    dur: dict[Key, float] = {}
    order: list[Key] = []
    for s in segments:
        if s.key not in dur:
            dur[s.key] = 0.0
            order.append(s.key)
        dur[s.key] += max(0.0, s.end - s.start)
    if not order:
        return None
    return max(order, key=lambda k: (dur[k], -order.index(k)))
