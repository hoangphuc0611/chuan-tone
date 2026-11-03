"""Kiểm tra tools/usability/task_times.py trên một nhật ký giả của buổi thử.

    python3 -m unittest discover -v tools/usability/tests
"""

import json
import os
import sys
import unittest
from datetime import datetime, timedelta

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import task_times as tt  # noqa: E402

T0 = datetime(2026, 11, 5, 9, 0, 0)


def line(sec, e, **data):
    return json.dumps({"t": (T0 + timedelta(seconds=sec)).isoformat(), "e": e, **data})


SESSION = [
    line(0, "session", version="1.0.0", model="iPhone14,2", os="iOS 26.1"),
    line(1, "screen", name="onboarding"),
    line(95, "onboarding.done", exit="addSong"),
    line(96, "screen", name="home"),
    line(140, "song.add", source="incoming", lyrics=False),
    line(150, "screen", name="sing"),
    line(160, "play", correction=None, genre="dry"),  # chưa bật chỉnh giọng
    line(170, "correction", level="natural"),
    line(175, "play", correction="natural", genre="dry"),
    line(200, "transpose", value=1),
    line(210, "transpose", value=-2),
    line(230, "fxmix", value=1.2),
    line(240, "genre", genre="bolero", correction="natural"),
    line(260, "error", message="Không mở được camera.", details="camera: denied"),
    line(300, "record.start"),
    line(332, "record.stop", seconds=32),
    line(333, "screen", name="review"),
    line(400, "photos.save", ok=False),
    line(420, "photos.save", ok=True),
    line(430, "screen", name="home"),
    line(440, "screen", name="sing"),
    line(455, "screen", name="home"),
    line(460, "screen", name="takes"),
    line(465, "screen", name="review"),
]


class TaskTimesTest(unittest.TestCase):
    def test_six_tasks(self):
        s = tt.sessions(tt.load(SESSION))
        self.assertEqual(len(s), 1)
        got = [(t.number, t.seconds) for t in tt.task_times(s[0])]
        self.assertEqual(got, [(1, 95), (2, 80), (3, 35), (4, 30), (5, 180), (6, 35)])
        self.assertEqual(tt.first_song_seconds(s[0]), 160)

    def test_unfinished_task_and_sessions(self):
        lines = SESSION[:9] + [line(600, "session", version="1.0.0"), line(605, "screen", name="home")]
        s = tt.sessions(tt.load(lines + ["{hỏng", ""]))
        self.assertEqual(len(s), 2)
        tasks = tt.task_times(s[0])
        self.assertEqual(tasks[1].seconds, 80)
        self.assertIsNone(tasks[2].seconds)  # chưa hạ tone
        self.assertEqual(tasks[3].start, tasks[2].start)  # tính từ lúc nhiệm vụ 3 bắt đầu

    def test_report(self):
        s = tt.sessions(tt.load(SESSION))[0]
        r = tt.report(s, timeline=True)
        self.assertIn("iPhone14,2", r)
        self.assertIn("| 5 | Thu, nghe lại, nhạc nhỏ, xuất video, lưu vào Ảnh | 4:00 | 3:00 |", r)
        self.assertIn("Mở app → phát nhạc lần đầu: 2:40", r)
        self.assertIn("Lỗi người thử thấy: 1", r)
        self.assertIn("[camera: denied]", r)
        self.assertIn("transpose  value=-2", r)


if __name__ == "__main__":
    unittest.main()
