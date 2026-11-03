#!/usr/bin/env python3
"""Thời gian từng nhiệm vụ của buổi thử ChuẩnTone, tính từ nhật ký thử nghiệm (M8 B7).

Nhật ký: Cài đặt → Nâng cao → "Ghi nhật ký thử nghiệm", xong buổi thử bấm "Chia sẻ nhật ký" →
AirDrop sang Mac. Mỗi dòng một sự kiện JSON {"t": giờ ISO, "e": loại, ...}.

    python3 tools/usability/task_times.py nhat-ky-thu-nghiem.jsonl            # phiên cuối
    python3 tools/usability/task_times.py nhat-ky-thu-nghiem.jsonl --session 2 --timeline

Mốc của 6 nhiệm vụ (docs/m8-plan.md mục 7) đoán theo sự kiện — đồng hồ của người điều phối vẫn là
số chính, bảng này để đối chiếu và tìm chỗ người thử loay hoay:
  1. hướng dẫn lần đầu xong (onboarding.done);
  2. thêm bài (song.add từ Tệp / "Mở bằng") rồi phát nhạc khi đang bật chỉnh giọng;
  3. hạ tone (transpose < 0);
  4. thể loại Bolero và "Vang" > 100%;
  5. thu → … → lưu video vào Ảnh (photos.save thành công);
  6. về Trang chủ rồi mở lại cả Màn hát lẫn Nghe lại.
Nhiệm vụ chưa xong thì nhiệm vụ sau tính từ lúc nhiệm vụ đó bắt đầu.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from datetime import datetime


@dataclass
class Event:
    t: datetime
    e: str
    data: dict


@dataclass
class Task:
    number: int
    name: str
    start: datetime
    end: datetime | None

    @property
    def seconds(self) -> float | None:
        return None if self.end is None else (self.end - self.start).total_seconds()


TASKS = [
    "Hướng dẫn lần đầu tới khi nghe được giọng mình",
    "Thêm bài và hát có chỉnh giọng",
    "Hạ tone",
    "Bolero, vang hơn",
    "Thu, nghe lại, nhạc nhỏ, xuất video, lưu vào Ảnh",
    "Về Trang chủ, mở lại bài và bản thu",
]


def load(lines) -> list[Event]:
    """Đọc các dòng JSONL; dòng hỏng bị bỏ."""
    out = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            j = json.loads(line)
            t = datetime.fromisoformat(j.pop("t"))
            e = j.pop("e")
        except (ValueError, KeyError, TypeError):
            continue
        out.append(Event(t, e, j))
    return out


def sessions(events: list[Event]) -> list[list[Event]]:
    """Tách theo mốc "session" (mỗi lần mở app khi đang bật nhật ký)."""
    out: list[list[Event]] = []
    for ev in events:
        if ev.e == "session" or not out:
            out.append([])
        out[-1].append(ev)
    return out


def _first(events, after, pred):
    for ev in events:
        if ev.t >= after and pred(ev):
            return ev
    return None


def _all_seen(events, after, preds):
    """Lúc mọi điều kiện trong preds đều đã gặp (sau after), None nếu chưa đủ."""
    seen = [None] * len(preds)
    for ev in events:
        if ev.t < after:
            continue
        for i, p in enumerate(preds):
            if seen[i] is None and p(ev):
                seen[i] = ev.t
        if all(s is not None for s in seen):
            return max(seen)
    return None


def task_times(session: list[Event]) -> list[Task]:
    if not session:
        return []
    t0 = session[0].t
    tasks: list[Task] = []

    def add(name_index, start, end):
        tasks.append(Task(name_index + 1, TASKS[name_index], start, end))
        return end if end is not None else start

    # 1. Hướng dẫn.
    done = _first(session, t0, lambda ev: ev.e == "onboarding.done")
    at = add(0, t0, done.t if done else None)
    # 2. Thêm bài (Tệp / "Mở bằng") rồi phát khi đang bật chỉnh giọng.
    added = _first(session, at, lambda ev: ev.e == "song.add"
                   and ev.data.get("source") in ("pick", "incoming"))
    sung = added and _first(session, added.t, lambda ev: ev.e == "play"
                            and ev.data.get("correction") is not None)
    at = add(1, at, sung.t if sung else None)
    # 3. Hạ tone.
    lower = _first(session, at, lambda ev: ev.e == "transpose" and ev.data.get("value", 0) < 0)
    at = add(2, at, lower.t if lower else None)
    # 4. Bolero + vang hơn.
    at = add(3, at, _all_seen(session, at, [
        lambda ev: ev.e == "genre" and ev.data.get("genre") == "bolero",
        lambda ev: ev.e == "fxmix" and ev.data.get("value", 0) > 1,
    ]))
    # 5. Thu … lưu vào Ảnh.
    saved = _first(session, at, lambda ev: ev.e == "photos.save" and ev.data.get("ok") is True)
    at = add(4, at, saved.t if saved else None)
    # 6. Về Trang chủ, mở lại bài + bản thu.
    home = _first(session, at, lambda ev: ev.e == "screen" and ev.data.get("name") == "home")
    start6 = home.t if home else at
    add(5, start6, _all_seen(session, start6, [
        lambda ev: ev.e == "screen" and ev.data.get("name") == "sing",
        lambda ev: ev.e == "screen" and ev.data.get("name") == "review",
    ]) if home else None)
    return tasks


def first_song_seconds(session: list[Event]) -> float | None:
    """Từ lúc mở app tới lần phát nhạc đầu tiên ở Màn hát (chỉ tiêu ≤ 3 phút, mục 6)."""
    if not session:
        return None
    play = _first(session, session[0].t, lambda ev: ev.e == "play")
    return None if play is None else (play.t - session[0].t).total_seconds()


def mmss(seconds: float | None) -> str:
    if seconds is None:
        return "chưa xong"
    s = int(round(seconds))
    return f"{s // 60}:{s % 60:02d}"


def report(session: list[Event], timeline: bool = False) -> str:
    out = []
    head = session[0].data if session and session[0].e == "session" else {}
    t0 = session[0].t if session else None
    out.append(f"Phiên lúc {t0:%d/%m %H:%M} — {head.get('model', '?')} {head.get('os', '')}, "
               f"app {head.get('version', '?')}" if t0 else "Phiên rỗng")
    out.append("")
    out.append("| # | Nhiệm vụ | Bắt đầu | Thời gian |")
    out.append("|---|---|---|---|")
    for task in task_times(session):
        out.append(f"| {task.number} | {task.name} | {mmss((task.start - t0).total_seconds())} | "
                   f"{mmss(task.seconds)} |")
    out.append("")
    out.append(f"Mở app → phát nhạc lần đầu: {mmss(first_song_seconds(session))} (chỉ tiêu ≤ 3:00)")
    errors = [ev for ev in session if ev.e == "error"]
    out.append(f"Lỗi người thử thấy: {len(errors)}")
    for ev in errors:
        out.append(f"  {mmss((ev.t - t0).total_seconds())}  {ev.data.get('message', '')}"
                   + (f"  [{ev.data['details']}]" if ev.data.get("details") else ""))
    if timeline:
        out.append("")
        out.append("Dòng thời gian:")
        for ev in session:
            extra = " ".join(f"{k}={v}" for k, v in ev.data.items())
            out.append(f"  {mmss((ev.t - t0).total_seconds())}  {ev.e}  {extra}".rstrip())
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log", help="file nhat-ky-thu-nghiem.jsonl")
    ap.add_argument("--session", type=int, default=0,
                    help="phiên thứ mấy (1 = đầu tiên; mặc định phiên cuối)")
    ap.add_argument("--timeline", action="store_true", help="in cả dòng thời gian")
    a = ap.parse_args(argv)
    with open(a.log, encoding="utf-8") as f:
        all_sessions = sessions(load(f))
    if not all_sessions:
        print("Nhật ký rỗng.", file=sys.stderr)
        return 1
    print(f"{len(all_sessions)} phiên trong nhật ký.\n")
    s = all_sessions[a.session - 1] if a.session else all_sessions[-1]
    print(report(s, timeline=a.timeline))
    return 0


if __name__ == "__main__":
    sys.exit(main())
