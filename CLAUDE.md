# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`fm-1-impressionism` is a custom firmware for the **M-VAVE FM-1** pocket synthesizer, scaffolded by forking [zvenson/dinghy](https://github.com/zvenson/dinghy) v0.4 (GPL-3.0-only; see `LICENSING.md` for full credits) and rebranding it. The sections below capture the FM-1 firmware ecosystem and conventions (from thefirmwarehub.com and the open-source FM-1 firmware community) that any future work here must follow, since this device has a very specific toolchain, flash layout, and distribution model that isn't discoverable from generic embedded-C conventions.

**Current state**: v1 of the real engine is implemented (`firmware/src/impressionism.c` + `firmware/src/planing.h`, see `ROADMAP.md` for the spec) — a drone voice plus 3 chord-tone voices that plan/re-root themselves on independent timers, no clocked sequencer, panel knobs wired per the roadmap. The pure logic (scales, chord templates, the planing picker, the trigger scheduler) is TDD'd in `tests/planing_test.c`, no platform dependency. Verified beyond host mode: this environment has no local JieLi toolchain/SDK (and no Docker), so a real `.fwsc` was built via CI (`gh workflow run release.yml`, not just a tag release) and **flashed to a real connected FM-1** with `tools/fm1_install.py` — confirmed via the USB-MIDI identity handshake (`--info` reports `FM-1_950 [running]`). That flash caught a real bug host-mode couldn't: `impressionism.c` had declared a file-scope `rng_state` colliding with one already in `firmware/src/libc.c`, which only gets compiled on the non-host path — fixed by renaming to `engine_rng` (see git history). **Confirmed working by ear on the real FM-1** (not just the identity handshake). **Not yet done**: the reactive pixel-art lily-pond screen (still plain status text). The FM-1 identity `FM-1_950` is still provisional against the Hub's catalogue — see the Identity section below.

## Mission

This is a **generative ambient** instrument, not a played-note synth: the goal is a continuously self-evolving wash of sound in the harmonic language of Debussy/Ravel-era Impressionism, built to sit on a shelf and drift rather than be performed note-by-note. Every engineering choice below (engine design, panel mapping, screen visuals) should serve that, not a groovebox or a keyboard voice.

**Harmonic palette** — constrain all generative logic to these; never fall back to functional diatonic major/minor harmony:

- _Scales_: whole-tone, major/minor pentatonic, and the "bright" modes (Lydian, Mixolydian, Dorian). Avoid harmonic/melodic minor and anything implying a V–I pull.
- _Chords_: extended and coloristic — maj7, 6/9, add9, sus2/sus4, 9th/11th/13th stacks, quartal (stacked-4ths) voicings. Not plain triads.
- _Motion_: parallel "planing" (sliding a whole chord shape up/down the scale, à la Debussy's parallel fifths/ninths) and common-tone voice leading, instead of functional progressions or cadences. Harmony should read as static/ambiguous, never goal-directed.
- A sustained pedal tone / drone voice is close to mandatory — the Impressionist pedal point that upper voices drift above.

**What this implies for the engine:**

- No hard clock-quantized sequencer. Each voice free-runs on its own slow, semi-random timer (random-walk or weighted-probability note/chord picker, loosely-periodic or Poisson-ish gating) so the texture breathes instead of looping on a grid — this is what makes it ambient/rubato rather than a groovebox.
- Synthesis is plain C in the 44.1kHz/256-frame audio ISR, no DSP coprocessor (see Firmware architecture below) — build voices as phase-accumulator sine/additive stacks, not anything needing heavy division or steep filtering, and budget voice count against the flash/RAM limits already noted.
- Timbre should read "soft focus": slow attack/release envelopes, detuned unison stacking, slow LFO drift on filter cutoff/amplitude. Avoid percussive transients or anything metronomic in the sound itself.
- Panel/screen: keys and encoders are macro controls over the generative system (scale/mode, density, drift rate, drone on/off, filter "color") rather than a note-by-note keyboard; the screen favors slow pastel color washes over hard meters/numbers.
- This direction also governs `fmhub.json`'s `what` description and `kinds`, and the eventual FM-1 identity/branding.

## Conventions

- Commit messages follow **[Conventional Commits](https://www.conventionalcommits.org/)** (`feat:`, `fix:`, `docs:`, `chore:`, etc.) — use this for every commit in this repo, not just releases. This isn't just style: `tools/next_version.py` parses commit subjects to decide whether (and how) to release, see below.
- Versioning follows **[SemVer](https://semver.org/)** (`MAJOR.MINOR.PATCH`) — **but every component is a single digit (0-9), no exceptions**. That's not a style choice: the FM-1 identity (`FM-1_9XN`/`FM-1_95N`, see Identity below) encodes the version's last digit directly, and `tools/build.py --release X.Y.Z`'s own regex requires exactly one digit each. A version that would need a two-digit component can't be built with this scheme as-is.

### Release pipeline (fully automated — no manual tagging)

Two workflows:

1. **`.github/workflows/test.yml`** — builds all board variants with the real JieLi toolchain and runs the full test suite, on every push and PR. Must pass before anything below runs.
2. **`.github/workflows/release.yml`** — triggers on `test.yml` completing successfully on `main` (`workflow_run`, not a parallel `push` trigger: a release should never land on an untested commit), or on a human pushing a `vX.Y.Z` tag directly. Two jobs:
   - **`version`** — for the `workflow_run` path, runs `tools/next_version.py`, which finds the last `vX.Y.Z` tag, scans commits since then, and applies the standard Conventional Commits bump rule: `fix:` → patch, `feat:` → minor, a `!` after the type or a `BREAKING CHANGE` footer → major, anything else (`docs:`, `chore:`, `ci:`, `refactor:`, ...) → **no release** (empty tag output; `firmware` below then doesn't run, which is normal, not a failure). If a bump would need a two-digit component, it fails loudly instead (exit 1) — that needs a human decision, not an auto-tag. For a direct tag push, it just passes that tag straight through.
   - **`firmware`** (`needs: version`, only runs if a tag exists) — builds the real `--release` packages, runs the host tests again, attests build provenance, and publishes the GitHub Release with the `.fwsc` files, at the tag `version` resolved.

   One workflow, not two chained by a cross-workflow dispatch: an earlier version of this needed that only because a plain tag push made with the default `GITHUB_TOKEN` doesn't trigger other workflows (GitHub's deliberate anti-recursion rule) — `needs:` between two jobs in the same workflow sidesteps that question entirely, nothing left to route around.

Net effect: merge a `feat:`/`fix:`/breaking commit to `main`, let CI pass, and a real tagged release with built packages appears with no further action. A `docs:`/`chore:`/`ci:`-only push releases nothing, by design.

**Caution learned the hard way**: `tools/next_version.py`'s own unit tests (`tests/next_version_test.py`) exist because two real bugs shipped bad tags on this pipeline's first real runs (a naive `BREAKING CHANGE` substring match firing on this script's own commit message explaining the rule, and an order-dependent bump comparison that let a newer `fix:` commit block an older `feat:` from ever being seen) — before changing `bump_for()` again, extend those tests first, the same mistake is easy to repeat in a new shape.

## Repo layout (scaffolded from Dinghy)

```
firmware/src/impressionism.c  # application layer — the instrument itself (audio + UI). THE file to change.
firmware/src/platform.c       # infra: boot, USB, MIDI, update loader, audio ISR — don't touch casually
firmware/src/store.h          # per-board flash storage layout (settings/blobs/slots); STORE_MAGIC/DATA_FAMILY here
firmware/hal/                 # hardware abstraction layer — the only place allowed to touch raw registers
tools/                        # build.py (device build), fm1_install.py (USB installer), fm1pkg_make.py, lz4blk.py,
                               # next_version.py (release versioning), dev_emulator.sh (local browser emulator)
site/                         # this project's own GitHub Pages content (firmwares.json), see .github/workflows/pages.yml
tests/run_host.sh             # host-mode test: compiles with the system cc, renders to WAV + screen PNGs
tests/run_tests.sh            # full test suite; needs a completed ./build.sh first
build.sh                      # device build entry point (wraps tools/build.py; needs the JieLi toolchain + SDK)
.github/workflows/release.yml # builds + attests + releases on a `vX.Y` tag push (self-contained, see below)
fmhub.json                    # Firmware Hub listing metadata
docs/                         # Dinghy's own API.md / MAKE-IT-YOURS.md / RULES.md / SMK37.md — still accurate upstream reference
LICENSE, LICENSING.md         # GPL-3.0-only + per-file credit table; keep both current, see "Who made what"
```

Build/test commands (as actually wired up in this repo now):

```
sh tests/run_host.sh                                                    # host-mode, no hardware/toolchain needed
sh tests/run_tests.sh                                                   # full suite; needs build/impressionism.fwsc first
./build.sh [--release X.Y] [--board fm1|smk37] [--model pro|elite|mke]  # device build -> build/impressionism.fwsc
python3 tools/fm1_install.py build/impressionism.fwsc                   # install over USB (needs: pip install mido python-rtmidi)
tools/dev_emulator.sh [port]                                            # serve build/impressionism.fwsc in a local
                                                                         #   fm1-emu (needs Rust/rustup; ./build.sh first)
```

`tools/dev_emulator.sh` is the fast edit/build/look loop for the engine and the screen without a real FM-1 or a CI round trip — the same `fm1-emu` build `.github/workflows/pages.yml` deploys (pinned to the same `FM1_EMU_REF`, keep the two in sync), pointed at your own local `./build.sh` output instead of the latest GitHub release. Clones `fm1-emu` into `.cache/` (gitignored), not committed here.

## Toolchain requirements

- **MCU**: JieLi PI32v2 core (AC79/AC791N family, "WL82" bootloader). Not a general-purpose target — code must go through JieLi's toolchain, not a stock ARM/clang/gcc pipeline.
- **JieLi Linux toolchain** (x86-64 only) — `tools/build.py`'s own docstring and `build.sh` are explicit about this: set `JIELI_TOOLCHAIN` to it (default `~/.jieli/toolchain`), or run `tools/get_toolchain.sh` to fetch it from pkgman.jieliapp.com (this is what `.github/workflows/release.yml` does in CI). On macOS, or anywhere non-Linux/x86-64, `build.sh` runs each toolchain step inside a `linux/amd64` Docker container instead (`JIELI_DOCKER=1`, needs Docker running) — this has **not been exercised in this session** (no Docker invoked, no toolchain downloaded).
- **AC79 AIoT SDK** — closed-source; default `~/fw-AC79_AIoT_SDK` (`AC79_SDK` env var). CI clones it from `gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK` at tag `AC79NN_SDK_V1.2.1_2023-12-13`; `tools/build.py` checks three of its files (`uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`) against known SHA-256s and only warns (doesn't fail) on a mismatch.
- Compile with `-Os` — required for USB-MIDI stability on real hardware.
- Build safety checks (enforced by `build.sh`) reject: 64-bit division, ROM calls, RAM overflow, and register access outside the HAL.

## Firmware architecture (what a build actually contains)

A firmware is one `.fwsc` package = two programs in sequence:

1. **The update loader** — a small helper (Felucca's loader, used by nearly all custom firmware, or M-VAVE's official loader) whose only job is to write the second program into flash. The FM-1 cannot overwrite the program it's currently running, so installing is always: running firmware starts the new loader → device restarts into the loader → loader writes the real firmware → device restarts again running it.
2. **The firmware itself** — the actual instrument, split as:
   - `app_audio(int32_t *out, uint32_t n)` — audio ISR, called with an n-frame (256 @ 44.1kHz, stereo interleaved, Q15 ±32767) buffer to fill. Must be fast: no flash access, no screen calls, no blocking. Use `volatile` for anything shared with the main loop.
   - `app_main(void)` — the main loop; never returns, must call `platform_poll()` every few ms (feeds watchdog, services USB/input).
   - Everything else (panel input, LEDs, 240×240 screen via direct draw calls or a `cv_*` canvas API, storage) is reached through `platform.c`'s HAL — see `docs/API.md` (in this repo, inherited from Dinghy) for the full function/constant reference, and `docs/RULES.md` for the hard build-time safety checks (`tools/build.py`'s `mmio_check`/`check`: no raw register literals or volatile pointer casts outside `firmware/hal/`, no inline asm, no ROM calls, no RAM/flash-slot overflow).

**Flash layout** (1 MiB total): app code+consts (568 KB budget), RAM data+bss (96 KB), a zeroed `.pool` section for big buffers (336 KB), 24 KB stack, plus the storage region above app space — settings sector (4 KB), 8 blobs (4 KB each), 8 slots (16 KB each), each with magic/length/CRC32. Flash erases take ~50 ms with interrupts off (up to 200 ms for a full slot) — only save when audio-silent and ≥2s after the last change; sectors tolerate ~100k erases.

**Identity**: every firmware announces an `FM-1_...` identity string over USB (`tools/build.py`'s `BOARD_PREFIX`/`PRODUCT`; `firmware/src/impressionism.c`'s `FELUCCA_ID` comes from that at compile time). This project currently uses **`FM-1_95N`** (dev build: `FM-1_950`) — chosen because Dinghy's own `docs/MAKE-IT-YOURS.md` lists `900` (SLOOP), `91N` (Felucca), `93N` (sloopDX), `97N` (zp12), `98N` (a Doom port) and `99N` (Dinghy) as taken, leaving `92`/`94`/`95`/`96` apparently free as of that doc — but it also says "there is no registry yet: look at the hub, and ask there if you are unsure." **`95` has not been confirmed against the Hub's live catalogue** (`thefirmwarehub.com/kb/#ids`, which renders client-side and wasn't checkable from this session) — verify it's still unused before any public release tag, and change `BOARD_PREFIX`/`PRODUCT` in `tools/build.py` if not.

## Distribution: FM-1 Firmware Hub

Getting listed at [thefirmwarehub.com](https://thefirmwarehub.com/) (full rules: [/kb/](https://thefirmwarehub.com/kb/)) requires:

- GPL-3.0 (or MPL-2.0-compatible) public source, with a unique `FM-1_` identity and the update loader bundled so users can switch away again.
- A `fmhub.json` at repo root:
  ```json
  {
    "what": "<=300 char plain-English description, no HTML/links",
    "author": "Your Name",
    "kinds": ["synth|groove|drums|chords|tracker|other"],
    "play": "https://.../play/",
    "editor": "https://.../editor/",
    "cheat": "https://.../cheatsheet.pdf",
    "image": "docs/card.png"
  }
  ```
  The Hub re-reads this every ~3 hours.
- A GitHub Actions release workflow for the "Built from source" attestation badge. **Correction to an earlier draft of this doc**: the Hub's `/kb/` page describes a _shared reusable workflow_ (`zvenson/fm1-build/.github/workflows/build.yml@v1`) as one way to get this, reasoning that "a project's own workflow can download anything during the build... and GitHub would still sign it." Dinghy's **own actual** `.github/workflows/release.yml` (now in this repo, verified by reading it directly rather than trusting a summary) does **not** use that shared workflow — it's self-contained: it fetches the JieLi toolchain and AC79 SDK itself, builds, runs `tests/run_tests.sh`, and calls `actions/attest-build-provenance@v2` directly on the `build/impressionism-*.fwsc` files, on a `vX.Y` tag push. Since we forked Dinghy's own workflow as-is, that's what this repo has; don't swap in the shared-workflow form without a reason.
- Real-hardware testing both directions (install from V15, and from another custom firmware) before submitting.

## Recovery / unbricking (useful when testing on real hardware)

- Soft recovery: hold **OCT−** during power-on to force update mode, then install anything via the Hub.
- Deep recovery (device only shows a "WL82 UBOOT1.00" USB disk): use the Hub's `fm1_rescue.py` (Linux, GPL-3.0, at `/rescue/fm1_rescue.py`) or [Quixotic7/MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) (Windows/Linux, MIT) to talk to the ROM bootloader directly.
- Hardware-level fallback for a totally unresponsive unit: [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter), an RP2040 board wired directly to the FM-1's USB lines.

## Key reference links

- Hub + full technical rules: https://thefirmwarehub.com/ , https://thefirmwarehub.com/kb/
- Official source mirror (V15, Apache-2.0 app / closed SDK): https://github.com/m-vave/fm1_open
- Starting template: https://github.com/zvenson/dinghy
- Multi-engine reference base: https://github.com/hugelton/Felucca (BSP split out to https://github.com/hugelton/felucca-bsp)
- Shared signed-build CI workflow: https://github.com/zvenson/fm1-build
- Reverse-engineering notes / update protocol: https://github.com/AL-255/FM-1-RE
- Curated list of the whole FM-1 firmware ecosystem: https://github.com/cicloid/awesome-fm-1
