"""Test bộ nhận dạng license (python -m unittest discover tools/licenses/tests)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_dart_licenses as c  # noqa: E402


class NhanDang(unittest.TestCase):
    def test_cac_loai(self):
        self.assertEqual(c.nhan_dang("MIT License\nPermission is hereby granted, free of charge, to"), "MIT")
        self.assertEqual(
            c.nhan_dang("Redistribution and use in source and binary forms ... Neither the name of"),
            "BSD-3-Clause",
        )
        self.assertEqual(c.nhan_dang("Redistribution and use in source and binary forms ..."), "BSD-2-Clause")
        self.assertEqual(c.nhan_dang("Apache License\nVersion 2.0, January 2004"), "Apache-2.0")
        self.assertEqual(c.nhan_dang("This software is provided 'as-is', without any express"), "Zlib")
        # MPL nhắc tới LGPL trong định nghĩa "Secondary License" — vẫn là MPL.
        self.assertEqual(
            c.nhan_dang("Mozilla Public License Version 2.0 ... GNU Lesser General Public License"),
            "MPL-2.0",
        )
        self.assertEqual(c.nhan_dang("GNU LESSER GENERAL PUBLIC LICENSE Version 3"), "LGPL")
        self.assertEqual(c.nhan_dang("GNU GENERAL PUBLIC LICENSE Version 3"), "GPL")
        self.assertEqual(c.nhan_dang("chữ lạ"), "?")

    def test_doc_lock(self):
        import tempfile

        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "pubspec.lock"
            p.write_text(
                "packages:\n"
                "  ffi:\n"
                "    dependency: transitive\n"
                "    description:\n"
                "      name: ffi\n"
                "    source: hosted\n"
                '    version: "2.1.4"\n'
                "  flutter:\n"
                "    dependency: \"direct main\"\n"
                "    source: sdk\n"
                '    version: "0.0.0"\n'
                "sdks:\n"
                '  dart: ">=3.10.0"\n',
                encoding="utf-8",
            )
            goi = c.doc_lock(p)
        self.assertEqual([g["name"] for g in goi], ["ffi", "flutter"])
        self.assertEqual(goi[0]["version"], "2.1.4")
        self.assertEqual(goi[0]["source"], "hosted")
        self.assertEqual(goi[1]["dependency"], "direct main")


if __name__ == "__main__":
    unittest.main()
