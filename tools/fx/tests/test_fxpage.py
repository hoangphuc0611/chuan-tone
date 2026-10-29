import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fxpage  # noqa: E402


class FxPageTest(unittest.TestCase):
    def test_page_has_every_preset_question_and_attribution(self):
        rows = [{"title": t, "key": k, "desc": d, "dry": ["a.wav", "b.wav"], "wet": [f"{k}_1.wav", f"{k}_2.wav"],
                 "dlufs": 0.1} for t, k, _, d in fxpage.PRESETS]
        html = fxpage.page(rows, ["nữ", "nam"])
        for _, key, _, _ in fxpage.PRESETS:
            for q in fxpage.QUESTIONS:
                self.assertIn(f'name="{key}.{q[0]}"', html)
            self.assertIn(f'name="{key}.ghichu"', html)
        self.assertIn("CC BY 4.0", html)
        self.assertEqual(html.count("<audio"), 2 * 2 * len(fxpage.PRESETS))

    def test_score_table(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "ket-qua.json"
            p.write_text(json.dumps({"bolero.vang": "Quá", "bolero.dat": "Chưa đạt", "bolero.ghichu": "đuôi dài"}),
                         encoding="utf-8")
            out = io.StringIO()
            with redirect_stdout(out):
                fxpage.main(["score", str(p)])
            text = out.getvalue()
        self.assertIn("| Bolero / Trữ tình | Quá | — | — | — | Chưa đạt | đuôi dài |", text)


if __name__ == "__main__":
    unittest.main()
