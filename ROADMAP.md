# Roadmap

This is the versioned plan for the generative-ambient engine itself (`firmware/src/impressionism.c`), on top of
the mission and conventions already fixed in `CLAUDE.md`. It follows this repo's SemVer convention: v1 below is a
deliberately narrow first slice, not the full mission palette — later minor versions expand it.

v1 was scoped in a `/grill-me` session; one default below was made by the agent from `CLAUDE.md`'s mission text
rather than an explicit answer, and is flagged as such.

**Status**: the audio engine below (drone + 3 chord voices, both timers, panel mapping, persistence) is
implemented, TDD'd (`tests/planing_test.c`), and flashed to a real connected FM-1 via a CI-built `.fwsc` —
confirmed running by its USB-MIDI identity (`FM-1_950`), not yet by ear. The reactive pixel-art screen below is
not built yet (still plain status text).

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
