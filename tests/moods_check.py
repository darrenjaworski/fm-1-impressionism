#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Checks the flash writes in the log of a host run of tests/scripts/moods.txt (tests/run_host.sh's output):

  python3 tests/moods_check.py LOG

The script saves mood 1's blob three times, and no other. Each write must land in the window below, which is the rule in
ROADMAP.md (item 5) worked out from the script's own times:
  press at   6500 ms, playing:               30 s later, so 36501 ms
  press at  50100 ms, playing, the dial turned to another mood at 50500 ms: 30 s later, 80101 ms, and still mood 1's blob
  press at  86100 ms, paused:                 2 s later, so 88101 ms
Each write is 7 bytes (settings_t). Standard library only. Exit 0 when all hold, 1 when one fails, 2 for an unreadable log.
"""
import re
import sys

WANT = [(36500, 36700), (80100, 80300), (88100, 88300)]   # ms windows, in order
BLOB = re.compile(r"^\s*(\d+) ms\s+blob (\d+) saved \((\d+) B\)\s*$")


def main(path):
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError as e:
        print(f"moods_check: {e}")
        return 2
    writes = [(int(m.group(1)), int(m.group(2)), int(m.group(3))) for m in map(BLOB.match, lines) if m]
    ok = True

    def check(good, what):
        nonlocal ok
        ok &= bool(good)
        print(f"moods_check: {what} {'ok' if good else 'FAIL'}")

    check(len(writes) == len(WANT), f"{len(WANT)} blob writes (found {len(writes)})")
    check(all(w[1] == 1 for w in writes), "every write is mood 1's blob, never the mood the dial had moved to")
    check(all(w[2] == 7 for w in writes), "every write is 7 bytes")
    for (t, _, _), (lo, hi) in zip(writes, WANT):
        check(lo <= t <= hi, f"write at {t} ms is inside {lo}..{hi} ms")
    print("moods_check: all ok" if ok else "moods_check: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]) if len(sys.argv) == 2 else 2)
