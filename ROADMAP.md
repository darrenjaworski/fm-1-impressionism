# Roadmap

This is the versioned plan for the generative-ambient engine itself (`firmware/src/impressionism.c`), on top of
the mission and conventions already fixed in `CLAUDE.md`. It follows this repo's SemVer convention: v1 below is a
deliberately narrow first slice, not the full mission palette — later minor versions expand it.

v1 was scoped in a `/grill-me` session; one default below was made by the agent from `CLAUDE.md`'s mission text
rather than an explicit answer, and is flagged as such.

**Status**: the audio engine below (drone + 3 chord voices, both timers, panel mapping, persistence) is
implemented, TDD'd (`tests/planing_test.c`), flashed to a real connected FM-1 via a CI-built `.fwsc`, and
**confirmed working by ear**. The reactive pixel-art screen below is not built yet (still plain status text).

## v1 — initial prototype

**Scope is deliberately narrow** (first-real-thing-on-hardware, not the full mission palette yet):

- Scales: **whole-tone and pentatonic only**, switchable. (Lydian/Mixolydian/Dorian come later.)
- Chords: **template-based planing**, exactly 2 templates (intervals in semitones above the root):
  - **maj7**: +4, +7, +11
  - **add9**: +4, +7, +14
  - Each retrigger picks a template and a random scale-degree transposition. (Quartal/sus voicings come later.)
- External MIDI-in: **ignored** — no responsiveness to incoming notes in v1. (Deferred, not decided against forever.)

### Voices (4 total)

- 1 **drone** = the chord's root/pedal tone.
- 3 **drifting voices** = the chord template's other 3 intervals above that root.
- **Flagged default, not an explicit answer**: per `CLAUDE.md`'s mission ("each voice free-runs on its own slow,
  semi-random timer"), the plan is for the **drone to also occasionally re-root itself**, on a much slower
  independent timer (minutes, not seconds) than the 3 color voices — rather than being a permanently fixed pedal
  once set. Confirm or override this before/while implementing.

### Persistence

Current scale/mode, density, drift rate, and drone on/off are saved to flash (`settings_save`/`settings_load`,
already provided by `platform.c`) and restored on boot — the instrument comes back the way it was left.

### Panel mapping

| Control         | Does                                   |
| --------------- | -------------------------------------- |
| `ENC_SELECT`    | scale select (whole-tone / pentatonic) |
| `ENC_ALGORITHM` | density (how often voices retrigger)   |
| `ENC_KNOB1`     | drift rate                             |
| `ENC_KNOB2`     | filter "color" (cutoff)                |
| `BTN_ARP`       | drone on/off                           |
| `MASTER`        | volume (unchanged from the stock demo) |

### Screen: reactive impressionist pixel art

An impressionist pixel-art lily-pond scene (Monet-ish), not a static image — plus a small status-text overlay
(current scale name) for legibility while testing. Built from the platform's existing `cv_pset`/`cv_rect`/`cv_line`/
`cv_text` primitives; deliberately blocky/low-fidelity ("pixel art"), not an attempt at smooth photographic
gradients.

Reactive mapping:

| Drives                                | Affects                                                                 |
| ------------------------------------- | ----------------------------------------------------------------------- |
| `ENC_KNOB2` (color), continuous       | overall scene color temperature/brightness                              |
| a chord-change event                  | lily-pad bloom color shift, at that instant                             |
| `ENC_KNOB1` (drift rate), continuous  | speed of the water-ripple/reflection animation                          |
| `ENC_ALGORITHM` (density), continuous | how many lily pads/ripples look "active" at once                        |
| `BTN_ARP` (drone on/off)              | presence of a warm sun-glow/reflection halo (off = cooler, dimmer pond) |

## Deferred to later minor versions

Everything in the mission not covered above, added incrementally once v1 is proven on real hardware:

- Lydian, Mixolydian, Dorian modes.
- Quartal (stacked-4ths) and sus2/sus4 chord templates, and 11th/13th extensions beyond maj7/add9.
- Slow LFO drift on filter cutoff/amplitude as an _automatic_ timbral layer (v1's `ENC_KNOB2` is a manual/live
  color control; the mission's "soft-focus... slow LFO drift" as something the instrument does on its own is not
  yet in scope).
- External MIDI-in responsiveness (the mission's original idea: an incoming note gently nudges the drone root /
  scale center, rather than 1:1 performance).
- A reactive-vs-static decision was already made _for_ v1 (reactive, see above) — no further open question there.
- SMK-37 board support: inherited from Dinghy, untested, not a priority (see `CLAUDE.md`).

## Out of scope / not yet decided for any version

- Any reverb/delay-style space effect (dry oscillators + envelope + the one color filter only, so far).
- Hub submission (identity `FM-1_95N` is provisional — unverified against the Hub's live catalogue; see `CLAUDE.md`).

## Later: an online playable version on this repo's GitHub Pages

Goal: a browser-playable emulator hosted at this repo's own GitHub Pages, like
[charlesvestal.github.io/fm1-amb/emu/](https://charlesvestal.github.io/fm1-amb/emu/) — not implemented yet, this
is the plan to start from.

**Researched, not yet decided between:** `fm1-amb`'s own emulator is actually [fm1-base](https://github.com/charlesvestal/fm1-base)
(the platform `fm1-amb` is built on, itself forked from `felucca-bsp`)'s built-in Emscripten web build of that
firmware's _own source_ (`tools/build.py APP_DIR --web`) — not applicable to us directly, since this repo is
forked from Dinghy, which has no web/Emscripten build target at all (would mean porting our platform layer to a
new target, a large undertaking, or migrating off Dinghy onto fm1-base entirely — not proposed).

The tractable path instead: two **generic** FM-1 CPU emulators exist that run any unmodified `.fwsc` package
directly (JIT'd to WebAssembly, no source port of our firmware needed at all):

- [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) — Rust→WASM, hosted at
  joakimeriksson.github.io/fm1-emu, explicitly supports adding a custom firmware via `web/firmwares-own.json`,
  actively documented (`web/build.sh`).
- [shcherbakov7/m-wave-fm1-browser-emulator](https://github.com/shcherbakov7/m-wave-fm1-browser-emulator) —
  also Rust→WASM (built on `simonjohansson/fm1-emulator`, GPL-3.0, license-compatible with this repo), smaller/
  less documented at the time of research.

**Open questions for whoever picks this up:** which of the two to vendor/fork into this repo (or whether to just
get listed in `fm1-emu`'s own hosted instance instead of self-hosting); how the emulator's copy of our `.fwsc`
stays in sync with releases (a GitHub Pages deploy step reading the latest `v*` release asset, most likely);
licensing/attribution for whichever emulator gets vendored in (both are GPL-3.0-compatible, but neither is
written by this project and needs its own credit in `LICENSING.md` if its code ends up in this repo).

**Decided and fully researched (2026-10-30): `fm1-emu`.** Better-documented customization path, actively
maintained. `site/firmwares.json` already exists in this repo — this project's own single-firmware catalog
override, in `fm1-emu`'s exact schema, `"default": "impressionism"`, `"package": "impressionism.fwsc"` (a
relative URL, resolved against this repo's own Pages origin, not fm1-emu's).

**The exact build, researched from `fm1-emu`'s own source and CI** (its `.github/workflows/pages.yml`
`Build the site` step calls `tools/build_site.sh`, which for a non-SMK-37 build is just `web/build.sh` — the
SMK-37/`fm1-sid` submodule parts aren't needed here):

1. Checkout this repo (path `impressionism/`) **and** `joakimeriksson/fm1-emu` at a pinned commit (path
   `fm1-emu/`) — pinned: `93cb7ee3f7008b27959ba73073b864e37efd7289` (`master`, as of this research). Bump that
   pin deliberately later, don't let it drift.
2. `rustup target add wasm32-unknown-unknown` (GitHub's `ubuntu-latest` runners ship Rust/cargo already).
3. `fm1-emu/web/build.sh "$GITHUB_WORKSPACE/dist"` — builds the `fm1-web` crate to WASM and copies the page
   (`index.html`, `style.css`, `main.js`, `persistence.js`, `worklet.js`, `jit.js`, `fwsc.js`,
   `firmwares.json`, plus a report PDF and an OG image) into `dist/`. This is GPL-3.0, same as this project;
   needs its own credit in `LICENSING.md` once actually vendored in (not done yet — the workflow was never
   committed, see below).
4. `gh release download --repo <this repo> --pattern 'impressionism-*.fwsc'`, pick the one that isn't an
   `*-smk37*` variant, copy it into `dist/impressionism.fwsc` (a stable name across releases, so
   `firmwares.json`'s URL never needs updating when a new version ships).
5. Overwrite `dist/firmwares.json` with this repo's own `site/firmwares.json`.
6. Publish `dist/` via the same `configure-pages`/`upload-pages-artifact`/`deploy-pages` steps the hello-world
   page already uses (`.github/workflows/pages.yml`), triggered on push to `site/**`, on a new GitHub Release
   (`release: types: [published]`, so a new version's build reaches the emulator too), and `workflow_dispatch`.

**Blocked, not done:** writing this into `.github/workflows/pages.yml` was refused by Claude Code's own
auto-mode classifier ("Untrusted Code Integration") — checking out and compiling a third party's source as
part of this repo's CI is a real supply-chain-risk pattern worth a human's explicit sign-off, and that wasn't
routed around. **A human needs to either grant that permission and ask again, or write the workflow directly**
from the steps above (every file/step/URL needed is spelled out here; nothing else to research first).
