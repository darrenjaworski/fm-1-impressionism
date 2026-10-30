#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compute the next SemVer tag from Conventional Commits since the last vX.Y.Z tag (CLAUDE.md's Conventions),
and push it. Standard rule: fix: -> patch, feat: -> minor, a `!` after the type or a BREAKING CHANGE footer ->
major; anything else (docs, chore, ci, test, refactor, ...) carries no defined bump (conventionalcommits.org) --
if nothing since the last tag has one, there's nothing to release.

This repo's identity scheme (tools/build.py's --release X.Y.Z, docs/MAKE-IT-YOURS.md) has room for exactly one
digit per component -- it IS the FM-1_95N identity's last digit. A bump that would need a two-digit component
fails loudly instead of producing a tag the real build can't use; that needs a human decision (a new identity
scheme, a manual tag, ...), not an auto-tag.

  tools/next_version.py [--dry-run]

Exit codes: 0 a tag was created (or there was nothing to release -- both are fine outcomes, not failures),
1 a bump would need a two-digit component (CI should show this red: it needs a person).
"""
import re
import subprocess
import sys


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def last_tag():
    try:
        return sh("git", "describe", "--tags", "--match", "v[0-9].[0-9].[0-9]", "--abbrev=0").strip()
    except subprocess.CalledProcessError:
        return None  # no matching tag reachable from HEAD: this is the first release


def commits_since(tag):
    rng = f"{tag}..HEAD" if tag else "HEAD"
    log = sh("git", "log", rng, "--format=%H%x01%s%x01%b%x02")
    return [c for c in log.split("\x02") if c.strip()]


CONVENTIONAL = re.compile(r"^(\w+)(\([^)]*\))?(!)?:\s*(.*)$")
# conventionalcommits.org: a breaking-change footer is its OWN line, "BREAKING CHANGE: " (or "BREAKING-CHANGE: ")
# at the start of it -- not just those words appearing anywhere in the body. A naive substring check matched
# this very script's own commit message once, which explains the rule in prose ("a `!` ... or BREAKING CHANGE:
# major") without declaring one; that's the bug this anchoring fixes.
BREAKING_FOOTER = re.compile(r"^BREAKING[ -]CHANGE: ", re.MULTILINE)


RANK = {"patch": 1, "minor": 2, "major": 3}


def bump_for(commits):
    """-> "major"/"minor"/"patch"/None, the highest bump found across all commits since the last tag (not
    cumulative per commit -- one feat and ten fixes is still just a minor bump, the usual semantic-release rule).

    Ranked explicitly by RANK and taking the max, not "first commit of a given type wins": `git log` lists
    newest first, so a short-circuiting `level = level or "minor"` -- set once, never overwritten -- let a fix:
    commit (seen first, since it's newest) permanently block an older feat: commit from ever upgrading patch to
    minor. That shipped once already (v0.0.1 got tagged for a commit set that should have been v0.1.0); this is
    what actually fixes it, not just patches the symptom, and tests/next_version_test.py locks in why."""
    best = None
    for c in commits:
        _, subject, body = (c.split("\x01") + ["", "", ""])[:3]
        m = CONVENTIONAL.match(subject)
        if not m:
            continue
        typ, _scope, bang, _desc = m.groups()
        if bang or BREAKING_FOOTER.search(body):
            return "major"  # nothing outranks this; stop looking
        lvl = "minor" if typ == "feat" else "patch" if typ == "fix" else None
        if lvl and (best is None or RANK[lvl] > RANK[best]):
            best = lvl
    return best


def main():
    dry = "--dry-run" in sys.argv
    tag = last_tag()
    base = tag[1:] if tag else "0.0.0"
    x, y, z = (int(p) for p in base.split("."))

    commits = commits_since(tag)
    if not commits:
        print(f"nothing new since {tag or '(the start)'}")
        return 0

    level = bump_for(commits)
    if level is None:
        print(f"{len(commits)} commit(s) since {tag or '(the start)'}, none with a defined bump "
              "(only docs/chore/ci/refactor/... -- see conventionalcommits.org): nothing to release")
        return 0

    if level == "major":
        x, y, z = x + 1, 0, 0
    elif level == "minor":
        y, z = y + 1, 0
    else:
        z = z + 1

    if x > 9 or y > 9 or z > 9:
        print(f"error: v{x}.{y}.{z} needs a two-digit component; tools/build.py's identity scheme (one digit "
              "each, docs/MAKE-IT-YOURS.md) can't encode this. A human needs to decide how to handle it -- not "
              "auto-tagging.", file=sys.stderr)
        return 1

    new_tag = f"v{x}.{y}.{z}"
    print(f"{tag or '(none)'} -> {new_tag} ({level}, from {len(commits)} commit(s))")
    if dry:
        return 0

    sh("git", "tag", "-a", new_tag, "-m", new_tag)
    sh("git", "push", "origin", new_tag)
    print(f"TAGGED={new_tag}")  # the one line a caller should grep for -- everything above is for humans, and
    # "nothing new since v0.1.0" or similar would otherwise false-match a naive `grep -oE 'v[0-9]\.[0-9]\.[0-9]'`
    return 0


if __name__ == "__main__":
    sys.exit(main())
