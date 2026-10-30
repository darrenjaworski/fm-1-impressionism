# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`fm-1-impressionism` is a from-scratch custom firmware for the **M-VAVE FM-1** pocket synthesizer. As of now the repo contains only a README — there is no firmware source checked in yet. The sections below capture the FM-1 firmware ecosystem and conventions (from thefirmwarehub.com and the open-source FM-1 firmware community) that any future work here must follow, since this device has a very specific toolchain, flash layout, and distribution model that isn't discoverable from generic embedded-C conventions.

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

- Commit messages follow **[Conventional Commits](https://www.conventionalcommits.org/)** (`feat:`, `fix:`, `docs:`, `chore:`, etc.) — use this for every commit in this repo, not just releases.
- Versioning follows **[SemVer](https://semver.org/)** (`MAJOR.MINOR.PATCH`). Note the Hub/CI release workflow above tags as `vX.Y` (two-part) — once this matters, prefer full `vMAJOR.MINOR.PATCH` tags and treat a breaking change to the audio engine, panel mapping, or storage layout as a MAJOR bump, new generative/engine features as MINOR, and fixes/tuning as PATCH.

## Recommended starting point

Do not build a firmware from nothing — the community convention is to fork a template:

- **[zvenson/dinghy](https://github.com/zvenson/dinghy)** — the recommended minimal starting template ("the smallest firmware for the M-VAVE FM-1 to build your own on"). Gives you updates/rescue, USB-MIDI, USB audio, panel/LED/screen drivers, and flash storage for free, plus a ~300-line example instrument (`dinghy.c`) to replace. This is almost certainly the right base for a single-instrument firmware like this one.
- **[hugelton/Felucca](https://github.com/hugelton/Felucca)** — a larger multi-engine base, useful as a reference for more complex synthesis engines, and the origin of the loader most custom firmwares use.

**Not yet scaffolded** — the layout and commands below are Dinghy's own conventions, for reference until this repo actually forks it in:

```
firmware/src/<name>.c   # application layer — the instrument itself (audio + UI)
firmware/src/platform.c # infra: boot, USB, MIDI, update loader, audio ISR — don't touch casually
firmware/src/store.h    # per-board flash storage layout (settings/blobs/slots)
firmware/src/hal/       # hardware abstraction layer
tools/                  # build.py, fm1_install.py (USB installer)
tests/run_host.sh       # host-mode test: renders to WAV + screen PNGs, no hardware needed
build.sh                # device build entry point
.github/workflows/release.yml  # calls the shared signed-release workflow on tag push
fmhub.json              # Firmware Hub listing metadata
```

Build/test commands (Dinghy conventions):

```
tests/run_host.sh                     # host-mode: run the firmware on your dev machine, emit WAV/PNG
./build.sh [--release X.Y] [--board fm1|smk37] [--model pro|elite|mke]   # device build -> build/<name>.fwsc
python3 tools/fm1_install.py build/<name>.fwsc   # install over USB
```

## Toolchain requirements

- **MCU**: JieLi PI32v2 core (AC79/AC791N family, "WL82" bootloader). Not a general-purpose target — code must go through JieLi's toolchain, not a stock ARM/clang/gcc pipeline.
- **JieLi Windows Toolchain 2.5.3** (bundles PI32v2 clang 4.0.1) — set `FM1_PI32_TOOLCHAIN` to its `pi32` folder. On CI/Linux, the shared build workflow installs JieLi's Linux toolchain instead (`JIELI_TOOLCHAIN` env var).
- **AC79 AIoT SDK** — closed-source binary SDK providing the device engine; cloned to `~/fw-AC79_AIoT_SDK` (`AC79_SDK` env var) by the shared CI workflow.
- Compile with `-Os` — required for USB-MIDI stability on real hardware.
- Build safety checks (enforced by `build.sh`) reject: 64-bit division, ROM calls, RAM overflow, and register access outside the HAL.

## Firmware architecture (what a build actually contains)

A firmware is one `.fwsc` package = two programs in sequence:

1. **The update loader** — a small helper (Felucca's loader, used by nearly all custom firmware, or M-VAVE's official loader) whose only job is to write the second program into flash. The FM-1 cannot overwrite the program it's currently running, so installing is always: running firmware starts the new loader → device restarts into the loader → loader writes the real firmware → device restarts again running it.
2. **The firmware itself** — the actual instrument, split as:
   - `app_audio(int32_t *out, uint32_t n)` — audio ISR, called with an n-frame (256 @ 44.1kHz, stereo interleaved, Q15 ±32767) buffer to fill. Must be fast: no flash access, no screen calls, no blocking. Use `volatile` for anything shared with the main loop.
   - `app_main(void)` — the main loop; never returns, must call `platform_poll()` every few ms (feeds watchdog, services USB/input).
   - Everything else (panel input, LEDs, 240×240 screen via direct draw calls or a `cv_*` canvas API, storage) is reached through `platform.c`'s HAL — see Dinghy's `docs/API.md` for the full function/constant reference once that file exists in-repo.

**Flash layout** (1 MiB total): app code+consts (568 KB budget), RAM data+bss (96 KB), a zeroed `.pool` section for big buffers (336 KB), 24 KB stack, plus the storage region above app space — settings sector (4 KB), 8 blobs (4 KB each), 8 slots (16 KB each), each with magic/length/CRC32. Flash erases take ~50 ms with interrupts off (up to 200 ms for a full slot) — only save when audio-silent and ≥2s after the last change; sectors tolerate ~100k erases.

**Identity**: every firmware announces an `FM-1_...` identity string over USB. The Hub convention is `FM-1_9XX` (taken: SLOOP 900, Felucca 91N, sloopDX 93N, zp12 97N, Dinghy 99N) — pick an unused one so the Hub can distinguish this firmware from others sharing a base.

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
- A GitHub Actions release workflow for the "Built from source" attestation badge — `.github/workflows/release.yml` calling the shared reusable workflow:
  ```yaml
  name: release
  on:
    push:
      tags: ["v*"]
  jobs:
    build:
      uses: zvenson/fm1-build/.github/workflows/build.yml@v1
      permissions:
        contents: write
        id-token: write
        attestations: write
      with:
        build: ./build.sh --release "${GITHUB_REF_NAME#v}"
        packages: build/*.fwsc
  ```
  Tagging `vX.Y` and pushing triggers a signed build; the Hub verifies the Sigstore attestation, SHA-256, and that the build came from a tagged commit via the shared workflow (not the repo's own, since that could smuggle in unreviewed binaries).
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
