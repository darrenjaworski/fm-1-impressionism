#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build fm1-emu locally (the same pinned commit as .github/workflows/pages.yml's FM1_EMU_REF) and serve this
# project's own locally-built firmware in it -- the fast loop for developing the generative engine/screen
# without a real FM-1 or a CI round trip:
#   edit firmware/src/*.c -> ./build.sh -> tools/dev_emulator.sh -> refresh the browser tab
#   tools/dev_emulator.sh [port]            default port 8642 (fm1-emu's own convention, web/build.sh)
# Needs: ./build.sh already run at least once (build/impressionism.fwsc must exist; see CLAUDE.md's Toolchain
# requirements for the JieLi toolchain/AC79 SDK that needs); Rust + rustup (adds the wasm32-unknown-unknown
# target itself, if missing); python3. fm1-emu is cloned into .cache/ (gitignored), not committed to this repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-8642}"
FM1_EMU_REF="93cb7ee3f7008b27959ba73073b864e37efd7289" # keep in sync with .github/workflows/pages.yml
CACHE="$ROOT/.cache/fm1-emu"
OUT="$ROOT/.cache/fm1-emu-dist"

[ -f "$ROOT/build/impressionism.fwsc" ] || {
  echo "dev_emulator.sh: no build/impressionism.fwsc -- run ./build.sh first (needs the JieLi toolchain + AC79 SDK, CLAUDE.md)"
  exit 1
}

if [ ! -d "$CACHE/.git" ]; then
  echo "dev_emulator.sh: cloning joakimeriksson/fm1-emu into .cache/ (once; later runs just update it)"
  git clone --quiet https://github.com/joakimeriksson/fm1-emu.git "$CACHE"
fi
git -C "$CACHE" fetch --quiet origin "$FM1_EMU_REF"
git -C "$CACHE" checkout --quiet "$FM1_EMU_REF"

if command -v rustup >/dev/null 2>&1; then
  rustup target add wasm32-unknown-unknown
else
  echo "dev_emulator.sh: no rustup -- fm1-emu/web/build.sh falls back to the system cargo, but on macOS" >&2
  echo "Homebrew's rustc has no wasm32 std (its own comment says so): if the build below fails on that," >&2
  echo "install rustup (https://rustup.rs) and re-run." >&2
fi

"$CACHE/web/build.sh" "$OUT" >/dev/null
cp "$ROOT/build/impressionism.fwsc" "$OUT/impressionism.fwsc" # this project's own firmwares.json expects this name
cp "$ROOT/site/firmwares.json" "$OUT/firmwares.json"

echo "dev_emulator.sh: http://localhost:$PORT  (Ctrl-C to stop)"
cd "$OUT" && exec python3 -m http.server "$PORT"
