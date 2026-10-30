#!/usr/bin/env python3
"""Generate version tag: 0.0.1-<unix_timestamp>-<commit_hash>"""
import subprocess
import sys
import time

try:
    commit_hash = subprocess.check_output(
        ["git", "rev-parse", "--short=7", "HEAD"], text=True
    ).strip()
    unix_timestamp = int(time.time())
    version = f"0.0.1-{unix_timestamp}-{commit_hash}"
    print(version)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
