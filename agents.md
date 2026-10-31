# agents.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`fm-1-impressionism` is a custom firmware for the **M-VAVE FM-1** pocket synthesizer, scaffolded by forking [zvenson/dinghy](https://github.com/zvenson/dinghy) v0.4 (GPL-3.0-only; see `LICENSING.md` for full credits) and rebranding it. The sections below capture the FM-1 firmware ecosystem and conventions (from thefirmwarehub.com and the open-source FM-1 firmware community) that any future work here must follow, since this device has a very specific toolchain, flash layout, and distribution model that isn't discoverable from generic embedded-C conventions.

**Current state**: The audio engine is implemented (`firmware/src/impressionism.c` + `firmware/src/planing.h`, see `ROADMAP.md` for the spec) — a drone voice plus 3 chord-tone voices that plan/re-root themselves on independent timers, no clocked sequencer, panel knobs wired per the roadmap. The pure logic (scales, chord templates, the planing picker, the trigger scheduler) is TDD'd in `tests/planing_test.c`, no platform dependency. ENC_SELECT cycles 6 scales; there are 8 chord templates, and each replan or reroot picks one that fits the new degree (ROADMAP.md, "Chord and scale expansion"). The boot seed mixes the ms counter, the tick count and two ADC reads; `SEED_SHOW=1` shows it on screen, and the power-on check on hardware is still pending. Verified beyond host mode: this environment has no local JieLi toolchain/SDK (and no Docker), so a real `.fwsc` was built via CI (`gh workflow run release.yml`, not just a tag release) and **flashed to a real connected FM-1** with `tools/fm1_install.py` — confirmed via the USB-MIDI identity handshake (`--info` reports `FM-1_950 [running]`). That flash caught a real bug host-mode couldn't: `impressionism.c` had declared a file-scope `rng_state` colliding with one already in `firmware/src/libc.c`, which only gets compiled on the non-host path — fixed by renaming to `engine_rng` (see git history). **Confirmed working by ear on the real FM-1** (not just the identity handshake). The engine as of `5beb646`: each colour voice attacks, holds (2.5-7 s), releases (`envelope.h`) and retriggers on its own timer, picking up the chord only when quiet. The drone sits an octave below the chord, about 6 dB under the colour voices, with slow per-voice pitch drift. Each voice's main partial has 2nd-3rd harmonics, and 4th-6th scaled by the note's level, so notes brighten at the start and warm as they decay. Colour voices have fixed stereo places. The Schroeder reverb (`reverb.h`: 4 parallel damped combs + 2 series feedback combs, decay ~4.5 s below 1 kHz and ~1.5 s at 1.8 kHz) replaces the old diffuser. BTN_PLAY fades all voices out over ~45 ms, freezes the timers, and resumes where they stopped. The worst audio ISR block is 1.3 ms with harmonics and stereo (measured on real FM-1 hardware, 2026-10-31); previously 0.6 ms before those changes (budget 2.9 ms). The drone-octave, drift, and BTN_PLAY fade were confirmed by ear on the FM-1; the colour-note envelope, harmonics, stereo, drone-level, and reverb tail changes were confirmed by ear in the local emulator. The reactive lily-pond screen's eye check on hardware is still pending. Host measurements against the reference are in `ROADMAP.md`. **No versioning ceiling**: timestamp-based tags allow unlimited releases without semantic version constraints. The FM-1 identity `FM-1_950` is still provisional against the Hub's catalogue — see the Identity section below.

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
- Panel/screen: keys and encoders are macro controls over the generative system (scale/mode, density, drift rate, drone on/off, filter "color") rather than a note-by-note keyboard; the screen is a Monet _Water Lilies_ pond that matches the mood: stationary green lilies with a tiny random wander over a continuous, slowly flowing water field, with constant slow motion that no control speeds up or slows down (see ROADMAP.md's "Screen"), never hard meters/numbers.
- This direction also governs `fmhub.json`'s `what` description and `kinds`, and the eventual FM-1 identity/branding.

## Conventions

- Commit messages follow **[Conventional Commits](https://www.conventionalcommits.org/)** (`feat:`, `fix:`, `docs:`, `chore:`, etc.) — use this for every commit in this repo. Release tags no longer depend on the commit type (see the pipeline below).
- Release tags are `0.0.1-{unix_timestamp}-{short_sha}`, generated by `tools/version_timestamp.py` for every passing push to `main`. The `0.0.1` prefix is fixed; the timestamp makes each tag unique and sortable, so there is no version-number ceiling. The FM-1 identity (`FM-1_950`, see Identity below) does not encode the release tag.

### Release pipeline (fully automated — no manual tagging)

Two workflows:

1. **`.github/workflows/test.yml`** — builds all board variants with the real JieLi toolchain and runs the full test suite, on every push and PR. Must pass before anything below runs.
2. **`.github/workflows/release.yml`** — triggers on `test.yml` completing successfully on `main` (`workflow_run`, not a parallel `push` trigger: a release should never land on an untested commit). Two jobs:
   - **`version`** — runs `tools/version_timestamp.py` to name the tag `0.0.1-{unix_timestamp}-{short_sha}`, then creates and pushes it. Every passing push to `main` gets a release, whatever the commit type (`docs:`, `chore:` included).
   - **`firmware`** (`needs: version`, only runs if a tag exists) — builds the real `--release` packages, runs the host tests again, attests build provenance, and publishes the GitHub Release with the `.fwsc` files, at the tag `version` resolved. Its last step explicitly runs `gh workflow run pages.yml` — `pages.yml`'s own `release: types: [published]` trigger does **not** fire on its own here, same `GITHUB_TOKEN`-authored anti-recursion rule as the tag push; confirmed by checking `pages.yml`'s actual run history (every run was `push`-triggered, never `release`-triggered, across several real releases) rather than assuming the trigger worked.

   One workflow, not two chained by a cross-workflow dispatch: an earlier version of this needed that only because a plain tag push made with the default `GITHUB_TOKEN` doesn't trigger other workflows (GitHub's deliberate anti-recursion rule) — `needs:` between two jobs in the same workflow sidesteps that question entirely, nothing left to route around.

Net effect: every push to `main` that passes `test.yml` gets a tagged release with built packages and no further action. This is by design: the release is the device build for flashing, not a version bump.

**Historical note**: `tools/next_version.py` (and its tests) implemented an earlier SemVer bump rule that shipped bad tags twice. The pipeline no longer uses it; `release.yml` uses `tools/version_timestamp.py`.

### C coding standards

`docs/RULES.md` is the authority on the chip (integers only, the build checks, the audio interrupt). These are the project's additions.

**Portability**

- Firmware headers include only `<stdint.h>`. The JieLi toolchain has no libc headers, and the host build won't catch this: `<string.h>` compiled locally and failed in CI.
- Avoid struct assignment and large initializers in firmware code: they can emit `memcpy` calls. Write fields one by one (see `set_defaults()`).
- File-scope names must not collide with `firmware/src/libc.c` or `platform.c`, which the host build doesn't always compile the same way. A collision on `rng_state` only showed up on the device build.
- Big buffers go in `.pool`, using the host/device `#ifdef` already in `impressionism.c`. It's zeroed at boot, so don't clear it by hand.

**Audio ISR**

- Decide per sample or per block deliberately. Anything that doesn't have to be per sample runs per block.
- No division or `%` in a per-sample loop: wrap ring-buffer positions with a compare.
- Every variable touched by both `app_audio()` and the main loop is `volatile`, with one writer.
- Adding per-sample work means re-measuring with `ISR_TIMING=1` on hardware. A measurement only applies to the code it was taken on.

**Fixed point**

- State the format of a value where it's declared (Q15, Q8, 32-bit phase).
- Check that an `int32_t` product can't overflow, using worst-case inputs.
- `>>` on a negative value rounds toward minus infinity. In a feedback loop that leaves a permanent offset, so account for it.

**Structure and style**

- Pure logic goes in a header with no platform dependency (`planing.h`, `envelope.h`, `reverb.h`), so it can be tested on the host. Raw registers stay in `firmware/hal/`.
- `static` for everything at file scope; `<stdint.h>` types; `u` suffix on unsigned constants; `/* */` comments; 4-space indent; `snake_case` functions, `UPPER_CASE` macros.
- Declare constants that shape the sound by name, with their unit and resulting time or level.
- Repeated blocks become a small `static inline` helper. Delete code and macros when they stop being used.
- Comments explain why, and must be true. A number in a comment (a time, a size, a decay) is measured or calculated, and changes when the code does.

### Testing guidelines

**What to test where**

- Pure logic: test first in `tests/*_test.c`, and add it to `tests/run_tests.sh`.
- Note scheduling and ISR integration: host renders with a script in `tests/scripts/`.
- Anything involving the real toolchain, hardware or sound: CI, then the FM-1.

**Writing tests**

- Expected values come from an independent source: a spec, a worked example or a measurement. An assertion that recomputes the code's own expression proves nothing.
- A test's name must describe what it checks. If it says T60, it measures T60.
- Don't loosen a failing test until you know why it fails. If the code is wrong, fix the code. If the expectation is wrong, say so and fix it with the reason. Never remove a check just to get a pass.

**Host renders**

- Render long enough to reach the behaviour under test. The default 6.5 s render plays no colour notes, so use at least 3 minutes for engine changes.
- If a change should alter the sound and the render is byte-identical, the render isn't testing it.
- Refactors must render byte-identical WAVs. Compare SHA-256 before and after.
- Sanity-check any measurement script before believing its numbers.

**Reporting results**

- `tests/run_tests.sh` only passed if it prints `ALL TESTS PASSED`. Locally it stops at `build/impressionism.bin` without a device build, so that's a partial run: rely on CI for the rest, and don't report it as passing.
- A passing host build says nothing about the device. Check that CI passed before flashing.
- State how something was verified: unit test, host render, emulator, or FM-1. "Confirmed by ear" applies only to the build you actually heard it on, and only once you've said so.
- Docs record measured values with what they were measured on. Re-measure when the code changes, and don't carry old figures into a "now" column.

## Repo layout (scaffolded from Dinghy)

```
firmware/src/impressionism.c  # application layer — the instrument itself (audio + UI). THE file to change.
firmware/src/platform.c       # infra: boot, USB, MIDI, update loader, audio ISR — don't touch casually
firmware/src/planing.h        # pure logic: scales, chord templates, the planing picker, note scheduler
firmware/src/envelope.h       # per-note attack/release envelope (colour voices), testable on the host
firmware/src/reverb.h         # Schroeder reverb: 4 parallel combs + 2 series feedback combs, testable on the host
firmware/src/store.h          # per-board flash storage layout (settings/blobs/slots); STORE_MAGIC/DATA_FAMILY here
firmware/hal/                 # hardware abstraction layer — the only place allowed to touch raw registers
tools/                        # build.py (device build), fm1_install.py (USB installer), fm1pkg_make.py, lz4blk.py,
                               # version_timestamp.py (release tags)
site/                         # this project's own GitHub Pages content (firmwares.json), see .github/workflows/pages.yml
tests/run_host.sh             # host-mode test: compiles with the system cc, renders to WAV + screen PNGs
tests/run_tests.sh            # full test suite; needs a completed ./build.sh first
build.sh                      # device build entry point (wraps tools/build.py; needs the JieLi toolchain + SDK)
.github/workflows/release.yml # builds + attests + releases on every passing push to main
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
- A GitHub Actions release workflow for the "Built from source" attestation badge. **Correction to an earlier draft of this doc**: the Hub's `/kb/` page describes a _shared reusable workflow_ (`zvenson/fm1-build/.github/workflows/build.yml@v1`) as one way to get this, reasoning that "a project's own workflow can download anything during the build... and GitHub would still sign it." Dinghy's **own actual** `.github/workflows/release.yml` (now in this repo, verified by reading it directly rather than trusting a summary) does **not** use that shared workflow — it's self-contained: it fetches the JieLi toolchain and AC79 SDK itself, builds, runs `tests/run_tests.sh`, and calls `actions/attest-build-provenance@v2` directly on the `build/impressionism-*.fwsc` files, on every passing push to `main`. Since we forked Dinghy's own workflow as-is, that's what this repo has; don't swap in the shared-workflow form without a reason.
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
