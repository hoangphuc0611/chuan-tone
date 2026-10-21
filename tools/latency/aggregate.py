#!/usr/bin/env python3
"""Gộp kết quả đo độ trễ (CSV sao chép từ màn "Đo độ trễ" của app) thành báo cáo Markdown.

Dùng:
    python3 tools/latency/aggregate.py tools/latency/data/*.csv > docs/latency-results.md
    pbpaste | python3 tools/latency/aggregate.py -          # đọc từ clipboard (macOS)

Mỗi file có thể chứa nhiều dòng header (khi dán nhiều lần) — các dòng header lặp được bỏ qua.
Chỉ dùng thư viện chuẩn của Python.
"""

from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from datetime import date

# Khớp LatencyPage.csvHeader trong app/lib/pages/latency_page.dart
COLUMNS = [
    "date", "brand", "model", "os", "route", "api", "sample_rate", "burst_frames",
    "buffer_frames", "measured_ms", "reported_ms", "confidence", "valid_bursts", "spread_ms",
]

ROUTE_VI = {
    "speaker": "Loa ngoài", "wired": "Tai nghe có dây", "bluetooth": "Bluetooth",
    "usb": "USB", "receiver": "Loa thoại", "unknown": "Chưa rõ",
}

# Chỉ tiêu ADR-002 khi TẮT chỉnh giọng (round-trip, ms).
TARGET_IOS = 15.0
TARGET_ANDROID_FLAGSHIP = 20.0
TARGET_ANDROID_MID = 30.0


def read_rows(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        handle = sys.stdin if path == "-" else open(path, newline="", encoding="utf-8")
        with handle:
            for rec in csv.reader(handle):
                if not rec or rec[0] == "date":
                    continue
                if len(rec) != len(COLUMNS):
                    print(f"Bỏ qua dòng sai số cột ({len(rec)}): {rec}", file=sys.stderr)
                    continue
                rows.append(dict(zip(COLUMNS, rec)))
    return rows


def verdict(os_name: str, route: str, ms: float) -> str:
    # Bluetooth có độ trễ codec ~150–250 ms, nằm ngoài chỉ tiêu (app tắt nghe lại giọng khi dùng BT).
    if route == "bluetooth":
        return "Không áp dụng (Bluetooth)"
    if os_name.lower().startswith("ios"):
        return "Đạt" if ms <= TARGET_IOS else f"Chưa đạt (> {TARGET_IOS:g})"
    if ms <= TARGET_ANDROID_FLAGSHIP:
        return "Đạt mức flagship"
    if ms <= TARGET_ANDROID_MID:
        return "Đạt mức tầm trung"
    return f"Chưa đạt (> {TARGET_ANDROID_MID:g})"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    rows = read_rows(argv[1:])
    if not rows:
        print("Không có dòng dữ liệu nào.", file=sys.stderr)
        return 1

    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for r in rows:
        groups[(r["brand"], r["model"], r["os"], r["route"], r["api"])].append(r)

    out = [
        "# Kết quả đo độ trễ round-trip",
        "",
        f"Cập nhật: {date.today().isoformat()} · {len(rows)} lần đo · {len(groups)} cấu hình máy.",
        "",
        "Đo bằng màn \"Đo độ trễ\" của app (chuỗi MLS, tương quan chéo, trung vị 5 lần). "
        "Chỉ tiêu theo `docs/adr/002-latency-targets.md` cho trường hợp **tắt chỉnh giọng**: "
        f"iOS ≤ {TARGET_IOS:g} ms, Android flagship ≤ {TARGET_ANDROID_FLAGSHIP:g} ms, "
        f"tầm trung ≤ {TARGET_ANDROID_MID:g} ms. Khi bật chỉnh giọng cộng thêm 10 ms.",
        "",
        "Lưu ý: đo bằng loa ngoài → micro khác đường tai nghe; số chính xác nhất cho đường tai "
        "nghe là đo bằng dongle loopback.",
        "",
        "| Máy | HĐH | Đường | API | Buffer (burst) | Số lần | Trễ đo (trung vị) | Thấp–cao | HĐH báo | Đánh giá |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for (brand, model, os_name, route, api), rs in sorted(groups.items()):
        measured = [float(r["measured_ms"]) for r in rs]
        reported = [float(r["reported_ms"]) for r in rs]
        med = statistics.median(measured)
        buf = rs[-1]["buffer_frames"]
        burst = rs[-1]["burst_frames"]
        out.append(
            f"| {brand} {model} | {os_name} | {ROUTE_VI.get(route, route)} | {api} | "
            f"{buf} ({burst}) | {len(rs)} | **{med:.1f} ms** | "
            f"{min(measured):.1f}–{max(measured):.1f} | {statistics.median(reported):.1f} ms | "
            f"{verdict(os_name, route, med)} |"
        )
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
