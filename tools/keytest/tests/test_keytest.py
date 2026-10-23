"""Test cho tools/keytest. Chạy: python3 -m unittest discover -v tools/keytest/tests

- Có cài mir_eval → kiểm chéo bảng điểm với mir_eval.key.weighted_score trên cả 24 × 24 cặp tone.
- Biến môi trường KEYLAB=<đường dẫn keylab thật> → thêm test đầu-cuối với keylab đã build.
"""

from __future__ import annotations

import csv
import io
import json
import math
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import dataset  # noqa: E402
import keytest  # noqa: E402
import mirex  # noqa: E402
import prelabel  # noqa: E402
import train_profile  # noqa: E402
from mirex import MAJOR, MINOR, Key, Segment  # noqa: E402

C, A = Key(0, MAJOR), Key(9, MINOR)
ALL_KEYS = [Key(t, m) for m in (MAJOR, MINOR) for t in range(12)]


class MirexScoreTest(unittest.TestCase):
    def test_relations(self):
        cases = [
            (C, Key(0, MAJOR), 1.0, mirex.CORRECT),
            (C, Key(7, MAJOR), 0.5, mirex.FIFTH),        # C → G: quãng 5 lên
            (C, Key(5, MAJOR), 0.0, mirex.FIFTH_DOWN),   # C → F: quãng 5 xuống
            (C, Key(9, MINOR), 0.3, mirex.RELATIVE),     # C → Am
            (A, Key(0, MAJOR), 0.3, mirex.RELATIVE),     # Am → C
            (A, Key(9, MAJOR), 0.2, mirex.PARALLEL),     # Am → A (bolero hay gặp)
            (A, Key(4, MINOR), 0.5, mirex.FIFTH),        # Am → Em
            (C, Key(2, MAJOR), 0.0, mirex.OTHER),
            (C, Key(0, MINOR), 0.2, mirex.PARALLEL),
            (C, Key(4, MINOR), 0.0, mirex.OTHER),        # C → Em: không phải tương đối
            (C, None, 0.0, mirex.MISSING),
        ]
        for ref, est, want, rel in cases:
            with self.subTest(ref=ref, est=est):
                self.assertEqual(mirex.score(ref, est), want)
                self.assertEqual(mirex.relation(ref, est), rel)

    def test_descending_fifths_option(self):
        self.assertEqual(mirex.score(C, Key(5, MAJOR), descending_fifths=True), 0.5)
        self.assertEqual(mirex.score(C, Key(7, MAJOR), descending_fifths=True), 0.5)

    def test_matches_mir_eval(self):
        try:
            import mir_eval.key
        except ImportError:
            self.skipTest("chưa cài mir_eval")
        for ref in ALL_KEYS:
            for est in ALL_KEYS:
                with self.subTest(ref=ref.name, est=est.name):
                    self.assertAlmostEqual(
                        mirex.score(ref, est),
                        mir_eval.key.weighted_score(ref.mir_eval_name, est.mir_eval_name))

    def test_names(self):
        self.assertEqual(Key(9, MINOR).name, "Am")
        self.assertEqual(Key(1, MAJOR).name, "C#")
        self.assertEqual(Key(10, MINOR).name, "Bbm")
        for text, pc in [("C", 0), ("c#", 1), ("Db", 1), ("F♯", 6), ("Bb", 10), ("Cb", 11),
                         ("E#", 5)]:
            self.assertEqual(mirex.parse_tonic(text), pc, text)
        for bad in ("H", "", "C##x", "Cx"):
            with self.assertRaises(ValueError, msg=bad):
                mirex.parse_tonic(bad)
        self.assertEqual(mirex.parse_mode("thứ"), MINOR)
        self.assertEqual(mirex.parse_mode("Major"), MAJOR)


class TimelineTest(unittest.TestCase):
    def test_boundary_off_by_four_seconds(self):
        ref = [Segment(0, 60, A), Segment(60, 100, Key(9, MAJOR))]
        est = [Segment(0, 64, A), Segment(64, 100, Key(9, MAJOR))]
        # 60 s đúng + 4 s song song (0,2) + 36 s đúng.
        self.assertAlmostEqual(mirex.timeline_score(ref, est), (60 + 4 * 0.2 + 36) / 100)

    def test_gap_in_estimate_scores_zero(self):
        ref = [Segment(0, 100, C)]
        est = [Segment(10, 100, C)]
        self.assertAlmostEqual(mirex.timeline_score(ref, est), 0.9)

    def test_estimate_beyond_reference_is_ignored(self):
        ref = [Segment(0, 100, C)]
        est = [Segment(0, 150, C)]
        self.assertAlmostEqual(mirex.timeline_score(ref, est), 1.0)

    def test_main_key(self):
        segs = [Segment(0, 30, A), Segment(30, 100, Key(9, MAJOR)), Segment(100, 120, A)]
        self.assertEqual(mirex.main_key(segs), Key(9, MAJOR))
        self.assertEqual(mirex.main_key([Segment(0, 10, A), Segment(10, 20, C)]), A)


def write(path: Path, text: str) -> Path:
    path.write_text(text, encoding="utf-8")
    return path


class DatasetTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_parse_time_and_modulation(self):
        self.assertEqual(dataset.parse_time("95.5"), 95.5)
        self.assertEqual(dataset.parse_time("1:35"), 95.0)
        self.assertEqual(dataset.parse_time("1:02:03"), 3723.0)
        for bad in ("1:75", "", "a", "1::2"):
            with self.assertRaises(ValueError, msg=bad):
                dataset.parse_time(bad)
        mods = dataset.parse_modulations("1:35:A:major; 160.5:Am ;2:50:Bb:thứ")
        self.assertEqual(mods, [(95.0, Key(9, MAJOR)), (160.5, A), (170.0, Key(10, MINOR))])

    def test_load_valid_csv_with_bom_and_semicolons(self):
        for name in ("a.mp3", "b.m4a"):
            (self.dir / name).write_bytes(b"")
        csv_path = self.dir / "dev.csv"
        csv_path.write_text(
            "﻿file;key;mode;modulation;bpm;genre;artist\n"
            "a.mp3;A;minor;\"1:35:A:major\";68;bolero;X\n"
            "b.m4a;C#m;;;76,5;ballad;\n"
            "\n", encoding="utf-8")
        ds = dataset.load(csv_path)
        self.assertEqual(ds.errors, [])
        self.assertEqual(len(ds.songs), 2)
        a, b = ds.songs
        self.assertEqual(a.key, A)
        self.assertEqual(a.modulations, [(95.0, Key(9, MAJOR))])
        self.assertEqual(a.bpm, 68.0)
        self.assertEqual(b.key, Key(1, MINOR))
        self.assertEqual(b.bpm, 76.5)
        tl = a.reference_timeline(200.0)
        self.assertEqual(tl, [Segment(0.0, 95.0, A), Segment(95.0, 200.0, Key(9, MAJOR))])
        self.assertEqual(a.reference_timeline(90.0), [Segment(0.0, 90.0, A)])

    def test_errors(self):
        (self.dir / "ok.mp3").write_bytes(b"")
        csv_path = write(self.dir / "dev.csv",
                         "file,key,mode,modulation,bpm\n"
                         "ok.mp3,H,major,,\n"                    # chủ âm sai
                         "ok.mp3,Am,major,,\n"                   # mâu thuẫn
                         "ok.mp3,A,minor,\"2:00:C:major;1:00:D:major\",\n"  # không tăng dần
                         "ok.mp3,A,minor,\"1:00:A:minor\",\n"   # chuyển sang chính nó
                         "ok.mp3,A,minor,,500\n"                 # bpm ngoài khoảng
                         "missing.mp3,C,major,,\n"               # thiếu file
                         "ok.mp3,C,major,,\n"                    # hợp lệ
                         "ok.mp3,D,major,,\n")                   # trùng file
        ds = dataset.load(csv_path)
        self.assertEqual(len(ds.songs), 1)
        self.assertEqual(len(ds.errors), 7, ds.errors)
        self.assertTrue(all(e.startswith("dòng ") for e in ds.errors))
        self.assertIn("dòng 2", ds.errors[0])

    def test_missing_column(self):
        ds = dataset.load(write(self.dir / "dev.csv", "file,mode\na.mp3,major\n"))
        self.assertIn("thiếu cột: key", ds.errors[0])


class EndToEndTest(unittest.TestCase):
    """keytest + keylab giả: kiểm cách tính điểm tổng, báo cáo, chặn chỉnh tham số trên test."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        for name in ("a.mp3", "b.mp3", "c.mp3", "d.mp3"):
            (self.dir / name).write_bytes(b"")
        self.csv = write(self.dir / "dev.csv",
                         "file,key,mode,modulation,bpm,genre\n"
                         "a.mp3,A,minor,,68,bolero\n"
                         "b.mp3,C,major,,120,ballad\n"
                         "c.mp3,A,minor,\"60:A:major\",,bolero\n"
                         "d.mp3,G,major,,,ballad\n")
        preds = {
            "a.mp3": {"key": {"root": 9, "mode": "minor"}, "bpm": 136.0},       # đúng, BPM ×2
            "b.mp3": {"key": {"root": 7, "mode": "major"}, "bpm": 121.0,       # quãng 5 lên
                      "timeline": [{"start": 0, "end": 50, "root": 7, "mode": "major"},
                                   {"start": 50, "end": 100, "root": 0, "mode": "major"}]},
            "c.mp3": {"key": {"root": 9, "mode": "major"},                     # timeline lệch 4 s
                      "timeline": [{"start": 0, "end": 64, "root": 9, "mode": "minor"},
                                   {"start": 64, "end": 100, "root": 9, "mode": "major"}]},
            "d.mp3": {"status": "error"},
        }
        self.preds = write(self.dir / "preds.json", json.dumps(preds))
        self.args_log = self.dir / "args.jsonl"
        os.environ["FAKE_KEYLAB_PREDICTIONS"] = str(self.preds)
        os.environ["FAKE_KEYLAB_ARGS_LOG"] = str(self.args_log)
        self.keylab = shlex.join([sys.executable, str(HERE / "fake_keylab.py")])

    def tearDown(self):
        os.environ.pop("FAKE_KEYLAB_PREDICTIONS", None)
        os.environ.pop("FAKE_KEYLAB_ARGS_LOG", None)
        self.tmp.cleanup()

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = keytest.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_run_scores_and_reports(self):
        report = self.dir / "report.md"
        per_song = self.dir / "per_song.csv"
        code, out, _ = self.run_main("run", "--csv", str(self.csv), "--keylab", self.keylab,
                                     "--out", str(report), "--per-song", str(per_song))
        self.assertEqual(code, 0)
        timeline_c = (60 + 4 * 0.2 + 36) / 100
        want = (1.0 + 0.5 + timeline_c + 0.0) / 4
        self.assertIn(f"**{want:.3f}**", out)
        self.assertEqual(report.read_text(encoding="utf-8"), out.rstrip("\n") + "\n")
        self.assertIn("| Chuyển tone giả (bài không chuyển tone) | 1/3 |", out)  # bài b
        # Timeline mọi bài: a 1; b đoán G 0–50 s (quãng 5: 0,5) rồi C 50–100 s (1) → 0,75;
        # c như điểm chính; d lỗi 0.
        self.assertIn(f"| Điểm timeline, mọi bài (đúng cái engine dùng khi chỉnh giọng) | "
                      f"{(1.0 + 0.75 + timeline_c + 0.0) / 4:.3f} |", out)
        self.assertIn("| BPM Acc1 / Acc2 (số bài có nhãn) | 50% / 100% (2) |", out)
        self.assertIn("lỗi: TC_ERR_INVALID_ARG", out)
        # Tone chính: a đúng, b quãng 5, c: đáp án Am 60 s > A 40 s → tone chính Am, đoán A
        # → song song 0,2; d lỗi.
        self.assertIn(f"| Chỉ tone chính | {(1.0 + 0.5 + 0.2 + 0.0) / 4:.3f} |", out)
        self.assertIn(f"| bolero | 2 | {(1.0 + timeline_c) / 2:.3f} | 50% |", out)
        self.assertIn("| ballad | 2 | 0.250 | 0% |", out)
        rows = per_song.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(rows), 5)
        self.assertTrue(rows[0].startswith("file,ref,est,score"))

    def test_min_score_fails(self):
        code, _, err = self.run_main("run", "--csv", str(self.csv), "--keylab", self.keylab,
                                     "--min-score", "0.9")
        self.assertEqual(code, 1)
        self.assertIn("< 0.9", err)

    def test_test_set_guard_and_log(self):
        test_csv = self.dir / "test.csv"
        test_csv.write_text(self.csv.read_text(encoding="utf-8"), encoding="utf-8")
        code, _, err = self.run_main("run", "--csv", str(test_csv), "--keylab", self.keylab,
                                     "--", "--profile", "kk")
        self.assertEqual(code, 2)
        self.assertIn("Từ chối", err)
        code, _, err = self.run_main("sweep", "--csv", str(test_csv), "--keylab", self.keylab,
                                     "--grid", "profile=kk")
        self.assertEqual(code, 2)
        self.assertFalse(self.args_log.exists(), "keylab không được chạy khi bị từ chối")

        for i in (1, 2):
            code, out, _ = self.run_main("run", "--csv", str(test_csv), "--keylab", self.keylab)
            self.assertEqual(code, 0)
            self.assertIn(f"Lần chạy bộ test thứ {i}", out)
        log = (self.dir / "test-runs.jsonl").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(log), 2)
        self.assertEqual(json.loads(log[0])["n"], 4)

    def test_sweep_passes_grid_to_keylab(self):
        code, out, _ = self.run_main("sweep", "--csv", str(self.csv), "--keylab", self.keylab,
                                     "--grid", "profile=kk,temperley", "--grid", "bass=1",
                                     "--", "-x", "y")
        self.assertEqual(code, 0)
        calls = [json.loads(line) for line in self.args_log.read_text().splitlines()]
        self.assertEqual(calls, [["-x", "y", "--profile", "kk", "--bass", "1"],
                                 ["-x", "y", "--profile", "temperley", "--bass", "1"]])
        self.assertIn("| profile=kk bass=1 |", out)


class FakeKeylabCase(unittest.TestCase):
    """Thư mục tạm + keylab giả đọc dự đoán từ preds.json."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.args_log = self.dir / "args.jsonl"
        os.environ["FAKE_KEYLAB_ARGS_LOG"] = str(self.args_log)
        self.keylab = shlex.join([sys.executable, str(HERE / "fake_keylab.py")])

    def tearDown(self):
        os.environ.pop("FAKE_KEYLAB_PREDICTIONS", None)
        os.environ.pop("FAKE_KEYLAB_ARGS_LOG", None)
        self.tmp.cleanup()

    def predict(self, preds: dict) -> None:
        path = write(self.dir / "preds.json", json.dumps(preds))
        os.environ["FAKE_KEYLAB_PREDICTIONS"] = str(path)

    def quiet(self, fn, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = fn(list(argv))
        return code, out.getvalue(), err.getvalue()


class PrelabelTest(FakeKeylabCase):
    def setUp(self):
        super().setUp()
        (self.dir / "audio" / "sub").mkdir(parents=True)
        for name in ("audio/a.mp3", "audio/sub/b.m4a", "audio/ghi-chu.txt"):
            (self.dir / name).write_bytes(b"")
        self.predict({
            "a.mp3": {"key": {"root": 9, "mode": "minor", "confidence": 0.8}, "bpm": 68.2,
                      "duration_sec": 200,
                      "timeline": [{"start": 0, "end": 95, "root": 9, "mode": "minor"},
                                   {"start": 95, "end": 200, "root": 9, "mode": "major"}]},
            "b.m4a": {"key": {"root": 0, "mode": "major", "confidence": 0.9}, "bpm": 120.0,
                      "bpm_confidence": 0.9},
        })

    def test_suggestions_round_trip_through_dataset(self):
        out = self.dir / "dev.csv"
        code, _, _ = self.quiet(prelabel.main, str(self.dir / "audio"), "--out", str(out),
                                "--keylab", self.keylab, "--genre", "bolero")
        self.assertEqual(code, 0)
        ds = dataset.load(out)
        self.assertEqual(ds.errors, [])
        # Đường dẫn tương đối so với thư mục chứa CSV.
        self.assertEqual([s.file for s in ds.songs], ["audio/a.mp3", "audio/sub/b.m4a"])
        a, b = ds.songs
        self.assertEqual(a.key, A)
        self.assertEqual(a.modulations, [(95.0, Key(9, MAJOR))])
        self.assertEqual(a.bpm, 68.0)
        self.assertEqual(a.genre, "bolero")
        self.assertEqual(b.key, C)
        # Chưa ai nghe xác nhận → nhắc từng dòng.
        self.assertEqual(len(ds.warnings), 2)
        self.assertIn("chưa nghe xác nhận", ds.warnings[0])
        rows = list(csv.reader(out.read_text(encoding="utf-8-sig").splitlines()))
        self.assertEqual(rows[1][8], "x")  # a: có chuyển tone → cần nghe kỹ
        self.assertIn("Am (tin cậy 0.80)", rows[1][7])
        self.assertEqual(rows[2][8], "")  # b: tone và BPM đều chắc chắn

    def test_blind_sheet_has_no_machine_hints(self):
        out = self.dir / "test.csv"
        code, _, _ = self.quiet(prelabel.main, str(self.dir / "audio"), "--out", str(out),
                                "--keylab", self.keylab, "--blind")
        self.assertEqual(code, 0)
        self.assertFalse(self.args_log.exists(), "gán mù: không được chạy keylab")
        rows = out.read_text(encoding="utf-8-sig").splitlines()
        self.assertEqual(rows[0], "file,key,mode,modulation,bpm,genre,notes")
        self.assertEqual(rows[1], "audio/a.mp3,,,,,,")

    def test_refuses_to_overwrite_labels(self):
        out = write(self.dir / "dev.csv", "file,key\n")
        code, _, err = self.quiet(prelabel.main, str(self.dir / "audio"), "--out", str(out),
                                  "--keylab", self.keylab)
        self.assertEqual(code, 2)
        self.assertIn("--force", err)
        self.assertEqual(out.read_text(), "file,key\n")


class TrainProfileTest(FakeKeylabCase):
    def test_learn_rotates_to_tonic(self):
        d_major = [0.0] * 12
        d_major[2] = 3.0   # D (chủ âm)
        d_major[9] = 1.0   # A (bậc V của D)
        e_minor = [0.0] * 12
        e_minor[4] = 2.0
        e_minor[7] = 2.0   # G (bậc ♭3 của Em)
        p = train_profile.learn([(Key(2, MAJOR), d_major, d_major), (Key(4, MINOR), e_minor, e_minor)])
        self.assertAlmostEqual(p["major"][0], 0.75)
        self.assertAlmostEqual(p["major"][7], 0.25)
        self.assertAlmostEqual(p["minor"][3], 0.5)
        self.assertEqual(p["bass_major"], p["major"])
        with self.assertRaises(ValueError):
            train_profile.learn([(Key(2, MAJOR), d_major, d_major)])  # thiếu bài thứ

    def test_cross_validation_and_profile_file(self):
        names = []
        preds = {}
        lines = ["file,key,mode"]
        for i in range(10):
            name = f"s{i}.mp3"
            (self.dir / name).write_bytes(b"")
            mode = "minor" if i % 2 else "major"
            preds[name] = {"key": {"root": i % 12, "mode": mode}}
            lines.append(f"{name},{mirex._NAMES[i % 12]},{mode}")
            names.append(name)
        self.predict(preds)
        csv_path = write(self.dir / "dev.csv", "\n".join(lines) + "\n")
        out = self.dir / "learned.txt"
        code, stdout, _ = self.quiet(train_profile.main, "--csv", str(csv_path), "--keylab",
                                     self.keylab, "--folds", "2", "--out", str(out))
        self.assertEqual(code, 0)
        self.assertIn("Kiểm chéo 2 phần", stdout)
        rows = [l for l in out.read_text().splitlines() if l and not l.startswith("#")]
        self.assertEqual(len(rows), 4)
        self.assertTrue(all(len(r.split()) == 12 for r in rows))
        calls = [json.loads(l) for l in self.args_log.read_text().splitlines()]
        self.assertIn("--global-chroma", calls[0])
        self.assertTrue(any("--profile-file" in c for c in calls[1:]))

    def test_refuses_test_set(self):
        csv_path = write(self.dir / "test.csv", "file,key,mode\n")
        code, _, err = self.quiet(train_profile.main, "--csv", str(csv_path), "--keylab",
                                  self.keylab)
        self.assertEqual(code, 2)
        self.assertIn("Từ chối", err)


@unittest.skipUnless(os.environ.get("KEYLAB"), "đặt KEYLAB=<đường dẫn keylab> để chạy")
class RealKeylabTest(unittest.TestCase):
    """keylab thật: định dạng JSON khớp với những gì keytest đọc."""

    def test_wav_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            wav = Path(tmp) / "sin.wav"
            rate, secs = 44100, 2
            with wave.open(str(wav), "wb") as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(rate)
                frames = bytearray()
                for i in range(rate * secs):
                    v = int(12000 * math.sin(2 * math.pi * 440 * i / rate))
                    frames += struct.pack("<hh", v, v)
                w.writeframes(bytes(frames))
            res = keytest.run_keylab([os.environ["KEYLAB"]], [str(wav), str(Path(tmp) / "x.mp3")],
                                     2, [])
            ok = res[str(wav)]
            self.assertEqual(ok["status"], "ok")
            self.assertAlmostEqual(ok["duration_sec"], 2.0, places=3)
            self.assertEqual(ok["sample_rate"], 44100)
            self.assertEqual(ok["analysis_rate"], 11025)
            self.assertIn("total", ok["timing_ms"])
            self.assertEqual(res[str(Path(tmp) / "x.mp3")]["status"], "error")
            song = dataset.Song(1, "sin.wav", wav, A)
            r = keytest.evaluate(song, ok)
            self.assertTrue(r.ok)
            # Chỉ 2 s < minVoicedSec (3 s) → keylab không kết luận tone ("key": null).
            self.assertIsNone(ok["key"])
            self.assertEqual(r.relation, mirex.MISSING)

            out = subprocess.run([os.environ["KEYLAB"], "analyze", str(wav)], check=True,
                                 capture_output=True, text=True).stdout
            self.assertEqual(json.loads(out)["channels"], 2)

            # Tham số dò tone mà keytest sweep dùng; tham số sai → mã 2.
            ok_args = ["--profile", "temperley", "--bass-weight", "0.5", "--frame-norm", "l1",
                       "--silence-db", "30", "--compression", "10"]
            subprocess.run([os.environ["KEYLAB"], "analyze", str(wav), *ok_args], check=True,
                           capture_output=True)
            for bad in (["--profile", "khong-co"], ["--bass-weight", "2"], ["--frame-norm", "x"]):
                code = subprocess.run([os.environ["KEYLAB"], "analyze", str(wav), *bad],
                                      capture_output=True).returncode
                self.assertEqual(code, 2, bad)


if __name__ == "__main__":
    unittest.main()
