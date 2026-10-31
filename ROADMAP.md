# Roadmap

This is the versioned plan for the generative-ambient engine itself (`firmware/src/impressionism.c`), on top of
the mission and conventions already fixed in `CLAUDE.md`. No version ceiling: release tags are timestamp-based
(`0.0.1-{unix_timestamp}-{short_sha}`), and v1 below is a deliberately narrow first slice, not the full mission
palette — later releases expand it.

v1 was scoped in a `/grill-me` session; one default below was made by the agent from `CLAUDE.md`'s mission text
rather than an explicit answer, and is flagged as such.

**Status**: the audio engine below (drone + 3 chord voices, both timers, panel mapping, persistence) is
implemented, TDD'd (`tests/planing_test.c`), flashed to a real connected FM-1 via CI-built `.fwsc` packages, and
**confirmed working by ear on the real hardware**. The reactive pixel-art screen is built (Monet palette, Monet
palette, see below), **confirmed working on the FM-1 display**. The Pages emulator is live and working. Remaining
before public Hub submission: measure ISR time post-harmonics, verify identity `FM-1_95N` against Hub catalogue,
complete the feature set per v2 below.

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

- A reverb with feedback or a wet/dry knob. The fixed Schroeder reverb (`firmware/src/reverb.h`, 4 parallel damped combs → 2 series feedback combs, ~4.5 s tail below 1 kHz) is stable. Measured worst audio ISR block on the FM-1 before harmonics: 0.6 ms (budget 2.9 ms, `ISR_TIMING=1` shows it on screen). **Needs re-measurement post-harmonics** (each voice now does 7 sine calls per sample).
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

## Dreamier sound: measured against the reference (2026-10-31)

Host renders, 3 minutes at default settings, first 15 s skipped. Targets are from the reference recording (a Debussy
"Reverie" on a modular synth). The ear is the final judge; these numbers guide the work.

| Metric | Before | Now | Target |
| --- | --- | --- | --- |
| Envelope range p95-p10 (1 s RMS) | 2.9 dB | 7.7 dB | 15+ dB |
| Spectral centroid | 143 Hz | 236 Hz | 350-500 Hz |
| 20-100 Hz band | -5 dB | -8 dB | -15 to -25 dB |
| 1.6-3.2 kHz band | -73 dB | -38 dB | -25 to -35 dB |
| L/R correlation | 0.58 | 0.51 | 0.3-0.5 |
| Clipped samples | 802 | 0 | 0 |

What changed to get here: colour notes attack, hold and release (`envelope.h`); the drone an octave lower and
about 6 dB under the colour voices; a slow ~11-cent pitch drift per voice; a 2nd to 6th harmonic on each voice,
scaled by the note's level; and a fixed stereo place per colour voice. Still open: the envelope range and centroid
are short of target.

## Next scope (v2+)

**Feature and sound improvements**, each cheap enough to ship incrementally:

### Chord and scale expansion

- **Chord templates**: add sus2 (+2, +7, +12), sus4 (+5, +7, +12), 6/9 (+4, +7, +9), and quartal (+5, +12, +17), bringing the template count to 6. Each retrigger still picks one at random. Tests in `tests/planing_test.c`.
- **Scales**: add minor pentatonic, Lydian, Mixolydian, Dorian (4 new scales, 6 total). ENC_SELECT cycles through them instead of toggling. **Fixes the fmhub.json claim** ("extended and quartal chords") — currently only maj7/add9 exist.
- **Common-tone voice leading**: after replanning, favour chord templates that share tones with the outgoing one. This formalizes the mission's "parallel planing / common-tone motion" as motion rules, not just random walks.

### Control and interface

v1 uses 4 of the panel's 7 encoders (`ENC_SELECT`, `ENC_ALGORITHM`, `ENC_KNOB1`, `ENC_KNOB2`) and 1 of 14 buttons
(`BTN_ARP`; `BTN_PLAY` is pause/resume, done above). `ENC_KNOB3`, `ENC_KNOB4` and `ENC_PRESETS` (docs/API.md) are
unused hardware, as is the entire 27-key keybed. Proposed assignment for the rest, thought through in a
2026-10-31 session:

- **`ENC_SELECT` (upgrade from toggle to cycle)**: `set.scale = (set.scale + 1) % SCALE_COUNT` instead of
  `^= 1u`, once the scale table grows past 2 entries (see Chord and scale expansion above). No new control,
  no new concept — falls out of the scale-table work for free. The status-text overlay needs 6 scale names
  instead of 2.
- **`ENC_ALGORITHM`**: unchanged — stays density (how often voices retrigger).
- **`ENC_KNOB3` — voice-leading / chord-motion amount**: weights how `plan_next()` (`planing.h`) picks the
  next chord template. At 0, pure random walk (today's behaviour); at 3, bias toward whichever remaining
  template shares the most absolute pitches with the outgoing chord. Mechanically: compute each candidate's
  resolved semitones, count overlaps with the current chord, weight the random pick by overlap count scaled
  by the knob. Pure main-loop logic (replans fire every few seconds, not per sample) — no ISR cost, testable
  in `tests/planing_test.c` without touching audio. This is the concrete implementation of the mission's
  "common-tone voice leading", which v1 only does as an unweighted random walk.
- **`ENC_KNOB4` — sleep timer. Decided (2026-10-31), fork resolved**: long-press cycles 5/15/30/60/off
  minutes (a continuous turn with no press changes nothing — this is a press-to-cycle control, not a
  continuous one, unlike the other knobs). On expiry, fade `filter_k` and all voice gates to 0 over the same
  ~45 ms curve `BTN_PLAY`'s pause already uses, then freeze timers the same way pause does. Main-loop only
  (a counter compared against `fm1_ms`) — zero ISR risk, ships without waiting on the ISR re-measurement
  above. Fits "sits on a shelf and drifts" literally; doubles as a night-lamp-style auto-off.
  Reverb size/damping (the other half of the original fork) is deferred, not dropped — see "Out of scope /
  not yet decided" below; it still needs the ISR re-measurement first and can take a different control later
  (e.g. `ENC_PRESETS`'s own long-press) without conflicting with this.
- **`ENC_PRESETS` — step through 4 saved "moods"**: turning it increments/decrements a `preset_index` (0–3)
  and immediately loads that slot's `{scale, density, drift, color, drone_on}` into `set` (today's single-slot
  struct, same shape, stored 4× instead of once). Uses 4 of the platform's existing 8 flash slots (`store.h`),
  each far bigger than the ~5-byte struct needs, but that's the HAL's granularity. Turning the encoder loads
  instantly; it doesn't save.
- **`BTN_SAVE` — commit current settings into the selected preset slot**: pairs with `ENC_PRESETS` above.
  Writes the live `set` struct into whichever slot the encoder is pointing at, reusing the existing
  dirty-flag/2-second-debounce flash-write path (`settings_save`) parameterised by slot index. A brief LED
  flash on `BTN_SAVE` confirms the write (flash erases are ~50 ms, felt not instant — `docs/RULES.md`).
- **Key selection (any of the 27 keys, F3–G5) — sets the drone root**: on a key-down edge
  (`fm1_input_note_edges()`), find the scale degree whose resolved pitch is nearest that key (keys don't all
  land exactly on scale degrees — e.g. a whole-tone scale skips half the keyboard), set `home_degree`
  accordingly, and call `reroot()` immediately instead of waiting for `next_root_ms`. The only proposal that
  touches the keybed, which v1 reads nowhere in `app_main()`. Still a macro control (press once, done), not
  note-by-note performance — consistent with the mission's "not a played-note synth".
- **No tempo/BPM control, by design**: checked for any existing tempo/clock concept in the codebase — none
  exists (no `tempo`, `bpm`, or `clock_` anywhere). This is deliberate, not a gap: the mission is explicitly
  "no hard clock-quantized sequencer", and density/drift already cover "how often" and "how far" without a
  shared clock tying voices together. The platform can see incoming MIDI clock bytes (`midi_get()`), but v1
  ignores all MIDI-in by design. A future "sync density to incoming MIDI clock" toggle would be a deliberate,
  narrow exception to "no clock" — not assumed here, flagged only if wanted later.

### Sound and measurement

- **Automatic soft-focus LFO** (deferred in v1): slow modulation (0.1–0.5 Hz) on filter cutoff and amplitude, independent per voice, adds the mission's "slow LFO drift" as an automatic layer.
- **Spectral balancing**: re-measure ISR on hardware post-harmonics, then adjust harmonic levels or envelope shape to hit the ROADMAP targets (envelope range 15+ dB, centroid 350–500 Hz). Current: 7.7 dB and 236 Hz respectively.
- **Reverb size/damping control** (if ISR measurement permits): a knob scaling the feedback/damping coefficient in `comb_step()` (`firmware/src/reverb.h`) rather than a true resize (fixed-length delay buffers can't resize at runtime). No control assigned yet — `ENC_KNOB4` went to the sleep timer instead (Control and interface, above); candidates are `ENC_PRESETS`'s long-press or a future encoder if the panel gains one.

### Ecosystem

- **MIDI out**: send the generated notes on MIDI channel 1 (note on/off, pitch bend for drift). Allows recording in a DAW or driving other synths.
- **Battery indicator**: display voltage on the status line (below the screen text); fade volume at <15% to warn before shutdown.
- **fmhub.json completion**: add "play" (the Pages URL), "image" (docs/screen.png), and "cheat" (a one-page PDF of panel controls).

## Pages emulator and workflow — done (2026-10-30)

Goal: a browser-playable version at this repo's GitHub Pages. Live at
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

**Workflow status**: the workflow is complete and working. On every push to `site/**` and every published release, `.github/workflows/pages.yml` builds `fm1-emu` at the pinned commit (`FM1_EMU_REF`), copies this repo's latest `.fwsc`, overwrites the emulator's `firmwares.json` with this project's catalog, and publishes to GitHub Pages. `FM1_EMU_REF` is updated manually, not automatically — fine for now.

