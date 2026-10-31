# Roadmap

This is the versioned plan for the generative-ambient engine itself (`firmware/src/impressionism.c`), on top of
the mission and conventions already fixed in `CLAUDE.md`. It follows this repo's SemVer convention: v1 below is a
deliberately narrow first slice, not the full mission palette — later minor versions expand it.

v1 was scoped in a `/grill-me` session; one default below was made by the agent from `CLAUDE.md`'s mission text
rather than an explicit answer, and is flagged as such.

**Status**: the audio engine below (drone + 3 chord voices, both timers, panel mapping, persistence) is
implemented, TDD'd (`tests/planing_test.c`), flashed to a real connected FM-1 via a CI-built `.fwsc`, and
**confirmed working by ear**. The reactive pixel-art screen below is built (Monet palette, see below), pending a by-eye check on hardware.

## Initial scope (v0.0.1)

**What's built** (the first playable version):

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

**Palette (user-specified, 2026-10-30, picked by eye from Monet's actual _Water Lilies_)** — five colours,
`firmware/src/scene.h`'s `MONET_*` constants, nothing else: `#3B668C`/`#5888A6` (deep/lit blue water),
`#285959`/`#678C4D` (two pad-leaf greens, picked per cell for texture), `#8451A6` (the one flower purple; the
second chord template is the same purple dimmed, not a different hue — there's only one flower colour in the
reference). The color knob brightens/darkens the water within this range; it doesn't shift hue (the reference
palette has no separate warm tone to shift toward).

Reactive mapping:

| Drives                                | Affects                                                                 |
| ------------------------------------- | ----------------------------------------------------------------------- |
| `ENC_KNOB2` (color), continuous       | water brightness, dark blue to lit blue-teal (not a temperature shift)  |
| a chord-change event                  | lily-pad bloom color shift, at that instant                             |
| `ENC_KNOB1` (drift rate), continuous  | speed of the water-ripple/reflection animation                          |
| `ENC_ALGORITHM` (density), continuous | how many lily pads/ripples look "active" at once                        |
| `BTN_ARP` (drone on/off)              | presence of a warm sun-glow/reflection halo (off = cooler, dimmer pond) |

## Deferred to later versions

Everything in the mission not covered above, to add incrementally:

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

- A reverb with feedback or a wet/dry knob. The fixed Schroeder reverb (`firmware/src/reverb.h`, 4 combs + 2 allpasses, ~7 s tail) replaces the old diffuser; measured worst audio ISR block on the FM-1 with it in: 0.6 ms (budget 2.9 ms, `ISR_TIMING=1` shows it on screen).
- Hub submission (identity `FM-1_95N` is provisional — unverified against the Hub's live catalogue; see `CLAUDE.md`).

## Deferred items — done (2026-10-30 session 2)

Three items from an earlier session's roadmap, scoped and implemented:

**Dreamier default character** ✅ — Detuned partials (+1/256, about 7 cents, a chorus shimmer) per voice for richer, more resonant tone.
Slower envelopes (attack 5.8s, release 9.4s) for sustained character. Sparser defaults: density=0, drift=0,
color=12 (was 1, 1, 16) for less churning, slower drift, darker tone. Reference was Mutable Instruments
Rings (resonance) + Clouds (diffusion) — this achieves the resonance via partials; diffusion deferred (slower
envelopes + lower defaults already deliver the "floating" quality). Commit: `36af233`.

**Backlit buttons** ✅ — Soft ambient glow (`fm1_led_key_glow`) on all 13 unused buttons: OCT_DOWN/UP, FX, SEL,
ENV, LFO, EDIT, GLO, HOME, SAVE, SEQ, PLAY, REC. BTN_ARP retains full brightness as drone state indicator.
Visual polish matching the soft-focus ambient aesthetic. Commit: `1184ac4`.

**`BTN_PLAY` pause/resume** ✅ — Pressing BTN_PLAY toggles a pause state that freezes every timer (re-plan, re-root,
and each colour voice's note timer) and fades all voices to silence over about 45 ms (the reverb tail rings out after that). Resume restarts the
timers where they stopped. BTN_PLAY LED shows state: lit=generating, off=paused.

## An online playable version on this repo's GitHub Pages — done (2026-10-30)

Goal: a browser-playable emulator hosted at this repo's own GitHub Pages, like
[charlesvestal.github.io/fm1-amb/emu/](https://charlesvestal.github.io/fm1-amb/emu/). Live at
<https://darrenjaworski.github.io/fm-1-impressionism/>.

**What it is**: [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) (GPL-3.0), a **generic** FM-1
CPU emulator (Rust→WASM/JIT) that runs any unmodified `.fwsc` package — not something built from this project's
own source. That distinction mattered: `fm1-amb`'s own emulator is actually
[fm1-base](https://github.com/charlesvestal/fm1-base) (the platform it's built on)'s Emscripten web build of
that firmware's _own_ source code — not applicable here, since this repo is forked from Dinghy, which has no
web/Emscripten target at all (porting our platform layer to one, or migrating off Dinghy onto fm1-base, would
both be far larger undertakings than this needed to be). `fm1-emu` sidesteps that: it boots our already-built
`.fwsc` directly, no source port of this firmware required. (The other generic candidate considered,
`shcherbakov7/m-wave-fm1-browser-emulator`, was smaller/less documented at research time — not used.)

**How it's wired up** (`.github/workflows/pages.yml`, `site/firmwares.json`): on every push to `site/**` and on
every published release, the workflow checks out this repo and `fm1-emu` at a pinned commit (`FM1_EMU_REF`) side
by side, builds `fm1-emu`'s own `web/build.sh` (the same one its own CI calls, via `tools/build_site.sh`, minus
the SMK-37/`fm1-sid` submodule parts this project doesn't need), fetches this repo's latest FM-1 `.fwsc` release
asset and copies it in as a stable `impressionism.fwsc`, overwrites the built page's `firmwares.json` with this
project's own single-firmware catalog (`site/firmwares.json`, `"default": "impressionism"`), and publishes that
as the Pages site. The emulator's own source is never committed to this repo's git history — built fresh from
source every deploy, credited in `LICENSING.md`, same reasoning as not vendoring the JieLi SDK.

**Not yet done**: bumping `FM1_EMU_REF` is a manual, deliberate action (no auto-update) — fine for now, revisit
if `fm1-emu` ships something this project wants sooner.

**Blocked, not done:** writing this into `.github/workflows/pages.yml` was refused by Claude Code's own
auto-mode classifier ("Untrusted Code Integration") — checking out and compiling a third party's source as
part of this repo's CI is a real supply-chain-risk pattern worth a human's explicit sign-off, and that wasn't
routed around. **A human needs to either grant that permission and ask again, or write the workflow directly**
from the steps above (every file/step/URL needed is spelled out here; nothing else to research first).

## claude tooling

- skill to make running local web emulator from a local firmware build
