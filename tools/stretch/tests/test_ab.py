"""Test tools/stretch/ab.py bằng beatlab giả (không cần build engine)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import ab  # noqa: E402

FAKE = [sys.executable, str(HERE / "fake_beatlab.py")]


class ParseList(unittest.TestCase):
    def test_paths_and_start(self):
        beats = ab.parse_list("# chú thích\n\ntestdata/a b.m4a 12.5\n/abs/c.m4a\n")
        self.assertEqual(beats[0], (ab.ROOT / "testdata/a b.m4a", 12.5))
        self.assertEqual(beats[1], (Path("/abs/c.m4a"), None))


class BuildAndScore(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "beats.txt").write_text("x/one.m4a 10\nx/two.m4a\n", encoding="utf-8")
        # beatlab giả là script Python: bọc qua một file chạy được.
        self.beatlab = self.dir / "beatlab"
        self.beatlab.write_text(f"#!/bin/sh\nexec {' '.join(FAKE)} \"$@\"\n", encoding="utf-8")
        self.beatlab.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self):
        out = self.dir / "ab"
        rc = ab.main(["build", "--list", str(self.dir / "beats.txt"), "--out", str(out),
                      "--levels", "-4,2", "--algos", "pv,signalsmith", "--dur", "5",
                      "--seed", "3", "--beatlab", str(self.beatlab)])
        self.assertEqual(rc, 0)
        return out

    def test_blind_page_and_answers(self):
        out = self.build()
        answers = json.loads((out / ab.ANSWERS).read_text(encoding="utf-8"))
        self.assertEqual(len(answers["items"]), 4)  # 2 beat × 2 mức
        self.assertEqual(answers["beats"][0]["start"], 10.0)
        self.assertAlmostEqual(answers["beats"][1]["start"], 60.0)  # 30% của 200 s
        page = (out / "index.html").read_text(encoding="utf-8")
        self.assertNotIn("signalsmith", page)
        self.assertNotIn("pv", page)
        for it in answers["items"]:
            self.assertEqual(sorted(it["labels"].values()), ["pv", "signalsmith"])
        # Mỗi bản nghe là đúng thuật toán / mức ghi trong đáp án.
        wavs = {p.read_text(encoding="utf-8") for p in out.glob("*.wav")}
        self.assertIn("signalsmith -4", wavs)
        self.assertIn("pv +2", wavs)
        self.assertIn("pv +0", wavs)  # bản gốc

    def test_score_counts_by_algorithm(self):
        out = self.build()
        answers = json.loads((out / ab.ANSWERS).read_text(encoding="utf-8"))
        results = {}
        for it in answers["items"]:
            for label, algo in it["labels"].items():
                results[f"{it['id']}.{label}.nhoe"] = "Rõ" if algo == "pv" else "Không"
                results[f"{it['id']}.{label}.meo"] = "Hơi"
                if algo == "signalsmith":
                    results[f"{it['id']}.thich"] = label
        table = ab.summarize(results, answers)
        self.assertEqual(table[("pv", -4.0)]["nhoe"], [0, 0, 2])
        self.assertEqual(table[("signalsmith", 2.0)]["nhoe"], [2, 0, 0])
        self.assertEqual(table[("signalsmith", 2.0)]["thang"], 2)
        self.assertEqual(table[("pv", 2.0)]["meo"], [0, 2, 0])


if __name__ == "__main__":
    unittest.main()
