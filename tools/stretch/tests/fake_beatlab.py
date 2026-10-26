#!/usr/bin/env python3
"""beatlab giả cho test: `fake_beatlab.py render <file> -o out.wav --semitones N --algo A …`.

Ghi file ra (nội dung = "algo semitones" để test kiểm), in 1 dòng JSON như beatlab thật.
"""

import json
import sys


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] != "render":
        return 2
    opts = dict(zip(args[2::2], args[3::2]))
    with open(opts["-o"], "w", encoding="utf-8") as f:
        f.write(f"{opts.get('--algo', 'pv')} {float(opts.get('--semitones', 0)):+g}")
    print(json.dumps({"file": args[1], "out": opts["-o"], "duration_sec": float(opts.get("--dur", 1)),
                      "song_sec": 200.0}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
