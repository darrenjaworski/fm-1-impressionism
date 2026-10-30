#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/next_version.py's bump_for(): pure function, no git needed (commits are constructed directly in the
same "hash\\x01subject\\x01body" shape commits_since() produces). Run: python3 tests/next_version_test.py

Written after two real bugs shipped (one tagged v1.0.0 instead of v0.1.0, the next v0.0.1 instead of v0.1.0) --
this should have existed from the start, same as every other pure-logic piece in this repo (planing.h, scene.h)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import next_version as nv  # noqa: E402

fails = 0


def check(ok, what):
    global fails
    print(f"next_version: {what:<70} {'ok' if ok else 'FAIL'}")
    fails += not ok


def commit(subject, body=""):
    return f"deadbeef\x01{subject}\x01{body}"


# the bug itself: a fix: commit (git log order: newest first) must not block an OLDER feat: from being seen
check(nv.bump_for([commit("fix: a bug"), commit("feat: a thing")]) == "minor",
      "fix seen before (newer than) feat still resolves to minor, not patch (the actual bug that shipped twice)")
check(nv.bump_for([commit("feat: a thing"), commit("fix: a bug")]) == "minor",
      "feat before fix: minor (the order this happened to work by luck before)")

check(nv.bump_for([commit("fix: a bug")]) == "patch", "fix alone: patch")
check(nv.bump_for([commit("feat: a thing")]) == "minor", "feat alone: minor")
check(nv.bump_for([commit("fix: a"), commit("fix: b"), commit("fix: c")]) == "patch",
      "several fixes, no feat: still just patch (not cumulative)")

check(nv.bump_for([commit("feat!: a breaking thing")]) == "major", "a ! after the type: major")
check(nv.bump_for([commit("feat: a thing", "BREAKING CHANGE: it broke")]) == "major",
      "a BREAKING CHANGE: footer at the start of its own line: major")
check(nv.bump_for([commit("fix: a bug", "BREAKING-CHANGE: also broke")]) == "major",
      "the BREAKING-CHANGE (hyphen) spelling also counts")
check(nv.bump_for([commit("feat!: a thing"), commit("fix: later")]) == "major",
      "major outranks everything else found alongside it, in either order")

# the OTHER real bug: prose that merely mentions the phrase, not at the start of a line, must not count
check(nv.bump_for([commit("docs: explain the rule",
                           "the rule (fix: patch, feat: minor, ! or BREAKING CHANGE: major, anything else...)")])
      is None, "BREAKING CHANGE appearing mid-line in prose (not a real footer) does not count as a bump")

check(nv.bump_for([commit("docs: update the readme")]) is None, "docs: alone has no defined bump")
check(nv.bump_for([commit("chore: tidy"), commit("ci: tweak workflow")]) is None,
      "chore:/ci: together still have no defined bump")
check(nv.bump_for([commit("docs: a"), commit("fix: b"), commit("chore: c")]) == "patch",
      "a real fix among no-op commit types still resolves to patch")
check(nv.bump_for(["not a conventional commit at all", commit("feat: a thing")]) == "minor",
      "a non-conventional subject (e.g. this repo's own \"first commit\") is skipped, not an error")

check(nv.bump_for([]) is None, "no commits at all: no bump")

print("next_version:", "FAILED" if fails else "all ok")
sys.exit(1 if fails else 0)
