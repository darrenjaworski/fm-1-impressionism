# Roadmap

What's shipped and what's next for the generative ambient engine (`firmware/src/impressionism.c`). The mission and
conventions are in [agents.md](agents.md). Releases are timestamp-tagged (`0.0.1-{unix_timestamp}-{short_sha}`) on every
passing push to `main`, so this plan is ordered by priority, not by version number.

## Where it stands

- **Working on real hardware:** the audio engine is flashed to an FM-1 from CI-built `.fwsc` packages and confirmed by ear.
- **Not yet heard or seen on hardware:** the six scales and eight chords, the new note holds, the soft-focus LFO, the
  rebalanced spectrum and the pond screen.
  The colour-note envelope, harmonics, stereo, drone level and reverb tail have only been heard in the emulator.
- **Biggest gap in the sound:** none on the host numbers, which are at or near every target
  ([Measurements](#measurements)). Whether it sounds right is unheard: the rebalanced spectrum is about 4.5 dB quieter
  overall and brighter, and the drone is much further back.
- **Biggest gap in the panel:** `ENC_KNOB3`, `ENC_KNOB4`, `ENC_PRESETS`, `BTN_SAVE` and the keybed do nothing yet.
- **Audio budget:** the worst ISR block was 1.3 ms of 2.9 ms when last measured, before the soft-focus LFO and one more
  `sine()` per sample per voice. It needs re-measuring on the FM-1.

## Up next, in order

The order follows dependencies first, then impact. Sound comes before moods because the moods are tuned by ear against
it. Anything that adds a saved setting comes before moods because moods store those settings.

| #   | Item                                                        | Impact | Size   | Audio ISR | Why it's here                                                          |
| --- | ----------------------------------------------------------- | ------ | ------ | --------- | ---------------------------------------------------------------------- |
| 1   | [Re-baseline](#1-re-baseline)                               | High   | Small  | No        | The numbers and ear checks are out of date; everything below uses them |
| 2   | [Soft-focus LFO](#2-soft-focus-lfo) (host done, unheard)    | Medium | Small  | Light     | A mission feature that moves the same numbers as item 3                |
| 3   | [Spectral balancing](#3-spectral-balancing) (host done)     | High   | Medium | Yes       | The largest gap to the reference, and every mood inherits the result   |
| 4   | [Voice leading and settings](#4-voice-leading-and-settings) | High   | Medium | No        | The last missing mission harmony; adds the saved fields moods need     |
| 5   | [Moods and `BTN_SAVE`](#5-moods-and-btn_save)               | High   | Medium | No        | Turns the panel into something a listener can use; needs items 3-4     |
| 6   | [Sleep timer](#6-sleep-timer)                               | Medium | Small  | No        | Independent and cheap; fits "sits on a shelf and drifts"               |
| 7   | [Key sets the drone root](#7-key-sets-the-drone-root)       | Medium | Small  | No        | First use of the keybed; completes the panel                           |
| 8   | [Hub submission](#8-hub-submission)                         | High   | Medium | No        | Reaches listeners; the controls and sound should be final first        |

After the Hub, in rough priority:

1. **MIDI out:** send the generated notes over USB with `midi_send`. Engine events already run in the main loop, which is
   what `midi_send` requires. Notes only: drift is per voice, so a single channel's pitch bend can't carry it.
2. **MIDI in:** an incoming note nudges the drone root. It reuses item 7's path, so it's small once that ships.
3. **Battery indicator:** `FM1_ADC_BATT` is raw, so it needs calibrating against measured voltages before it can show a
   percentage.
4. **11th and 13th chords:** the mission lists them, but a chord has only 3 colour voices, so each needs a voicing that
   drops tones. Whole-tone needs checking too, since only 2 templates fit it.
5. **Reverb damping control:** scale the feedback or damping in `comb_step()` (`firmware/src/reverb.h`); the delay buffers
   can't change size at runtime. No control is free for it, because every encoder is assigned and encoders have no push.
6. **SMK-37 support:** inherited from Dinghy, with host tests only. Not a priority.

### 1. Re-baseline

- **Done (host):** the [measurements](#measurements) are re-rendered with a script that has a self-test, and the note
  rates are re-run (unchanged).
- **To do, on the FM-1:** re-measure the ISR with `ISR_TIMING=1`, since the 1.3 ms figure predates the drift fix. Check by
  ear: the six scales and eight chords, the new holds, the colour-note envelope, harmonics, stereo, drone level and reverb
  tail. Check by eye: the pond.
- **To do:** measure the reference recording with `tools/measure_render.py`, so the targets use the same definitions.
- **Decide:** whether the D2 to D3 step in [note rate](#measurements) needs fixing. It's 19% (18.6 to 22.1 notes per
  minute), but density also halves the chord-move time at D3 (3-7 s to 1.2-3.5 s). Leave it unless it's hard to hear on the
  FM-1.

### 2. Soft-focus LFO

The mission's slow drift as something the instrument does on its own. Done and tested on the host; not heard on the FM-1.

- **Amplitude, per voice:** each voice's drift LFO (`lfo_ph`, 0.07-0.27 Hz), read a quarter cycle late so level and pitch
  don't move together, scales that block's level (`amp_swell()`, `synth.h`). A note only dips, by up to 12 dB
  (`LFO_AMP_DEPTH`), so it never gets louder than before.
- **Filter, global:** one low-pass sits on the final mix, so cutoff can't be modulated per voice without a filter per
  voice. A separate 0.11 Hz LFO swings the `ENC_KNOB2` coefficient by +-40% around its setting (`filter_sweep()`,
  `LFO_FILTER_DEPTH`). At full open it can only dip.
- **Depth is a starting guess, to tune by ear.** The envelope range responds sharply: with the filter sweep on, depth 0
  gives 10.95 dB, 0.25 gives 10.96, 0.5 gives 11.30, 0.73 gives 13.74 and 0.92 gives 18.65. 0.75 was chosen as a clear
  effect that stops short of notes fading nearly to silence, not to hit the number. The filter sweep barely moves any
  measured figure, so its depth is a by-ear choice too.
- **Cost:** one extra `sine()` per voice and one `filter_sweep()` per block. The ISR hasn't been re-timed on the FM-1.

### 3. Spectral balancing

Done and tested on the host; not heard on the FM-1, and the ISR isn't re-timed. The ear is the final judge.

Where the problem was: with the drone off, the 20-100 Hz band falls from -3.5 dB to -66 dB, so the drone held nearly all
the low-end power. And a centroid of about 250 Hz held whatever the main partial did, because the detuned partial was a
pure fundamental at full level.

- **Drone level:** `DRONE_FULL` 4000 to 1500, about 15 dB under a full colour note, down from 6 dB. Level and band at
  each setting (otherwise as before): 4000 gives -3.5 dB, 2000 -7.7, 1000 -13.1, 500 -19.0. The envelope range rose with
  it, because the drone had been filling the gaps between notes.
- **Main partial:** fundamental 0.40, 2nd 0.35, 3rd 0.25 (was 0.70, 0.20, 0.10).
- **Upper partials:** 4th-6th at four times their old level (`HARM_4` to `HARM_6`).
- **Detuned partial:** 0.40 fundamental and 0.60 second harmonic (`DET_FUND`, `DET_2`), where it was a pure fundamental.
  With the other changes in, a pure fundamental gave a centroid of 280 Hz, 0.6/0.4 gave 354 Hz, 0.4/0.6 gave 407 Hz and
  all second harmonic gave 453 Hz. 0.4/0.6 keeps some of the chorus on the fundamental.
- **Cost:** one more `sine()` per sample per active voice.
- **Headroom:** peak -9.8 dBFS with no clipped samples, including the worst case (`tests/scripts/worstcase.txt`:
  density 3, drift 3, colour fully open, all six scales).
- **Quieter:** the overall RMS fell 4.5 dB, from -24.9 to -29.4 dBFS, so MASTER needs turning up. There's room to add
  gain, since the peak has about 10 dB of headroom.
- **Brighter when open:** at colour 31 the 1.6-3.2 kHz band is -17.6 dB in the worst-case render, above the target range.
  At the default colour 12 it's -30 dB.
- **Not tuned by ear:** the targets are approximate (see [Measurements](#measurements)) and every number above is a
  starting guess. The drone's level in particular is a judgement: at 1200 the band is -16.0 dB, inside the target.

### 4. Voice leading and settings

**`ENC_KNOB3`, voice leading, 0-3:** weights `pick_chord()` (`planing.h`) towards templates that share pitches with the
outgoing chord. At 0 it stays the uniform pick it is today. At 3 it favours the most shared pitches. It still picks only
from templates that fit the degree, so whole-tone has just 2 to choose from. Main-loop logic, tested in
`tests/planing_test.c`. The status row already has a `VOICE --` slot.

**Saved settings:** append `voice_lead` and `mood` to `settings_t`, and extend `settings_valid()`. A changed size reads as
"nothing saved", so everyone's settings reset to defaults once. That's cheap now and costs real users after the Hub, so
make all the layout changes in this one step.

### 5. Moods and `BTN_SAVE`

**`ENC_PRESETS` steps through 8 moods.** Turning it loads a mood straight into `set`; it doesn't save.

- **Applying a mood:** go through the same steps as the knobs, including `engine_scale_changed()` when the scale differs
  and `color_to_filter_k()` for the filter.
- **Storage:** one blob per mood (`blob_load`/`blob_save`, `store.h`), holding a `settings_t`. A blob is one 4 KiB sector,
  so a save erases about 50 ms, against about 200 ms for a slot's four sectors, and never rewrites another mood.
- **Why 8:** it's a usability choice, not a storage limit. One blob could hold about 800, but a dial is only useful if its
  stops can be learned.

**Moods are named after pieces, not composers.** Debussy and Ravel share too much harmonic language for a composer to pin
down a sound, while a piece gives concrete choices. Each mood's factory settings sit in a const table, used while its
blob is empty. The numbers are starting guesses, to tune by ear after item 3, and each mood also needs a voice-leading
value:

| #   | Mood                                    | Scale            | Density | Drift | Colour | Drone |
| --- | --------------------------------------- | ---------------- | ------- | ----- | ------ | ----- |
| 1   | Voiles (Debussy)                        | Whole-tone       | 0       | 0     | 10     | On    |
| 2   | Clair de lune (Debussy)                 | Pentatonic       | 1       | 1     | 16     | Off   |
| 3   | Arabesque No. 1 (Debussy)               | Pentatonic       | 3       | 2     | 28     | Off   |
| 4   | Pagodes (Debussy)                       | Pentatonic       | 2       | 1     | 24     | Off   |
| 5   | La cathédrale engloutie (Debussy)       | Pentatonic       | 0       | 2     | 8      | On    |
| 6   | Sarabande, _Pour le piano_ (Debussy)    | Dorian           | 1       | 2     | 14     | Off   |
| 7   | Pavane pour une infante défunte (Ravel) | Mixolydian       | 1       | 1     | 12     | On    |
| 8   | Le gibet (Ravel)                        | Minor pentatonic | 0       | 0     | 6      | On    |

The moods evoke the pieces rather than quote them: the notes stay generated, and the root is fixed around C
(`ROOT_BASE_NOTE`), so no mood is in its piece's key. There's no major scale, so major pentatonic stands in for the
major-key pieces: all its notes belong to the major scale, while Lydian and Mixolydian each have one that doesn't. Ravel's
two are the loosest fits, and the Sarabande's Dorian should be checked against the score. Lydian is unused.

**`BTN_SAVE` writes the live settings to the selected mood's blob.** It follows the same rule as settings today, because
an erase drops the sound for about 50 ms: save 2 s after the press if paused, otherwise wait up to 30 s. `BTN_SAVE`'s LED
stays lit while a save is pending and goes out when it's written.

**Names:** a piece's name only describes its factory settings, so it shows while the blob is empty, and `MOOD 1` to
`MOOD 8` once saved over. Where it goes on the screen is still to decide; the three status rows are full.

### 6. Sleep timer

**`ENC_KNOB4`:** each detent steps through off, 5, 15, 30 and 60 minutes, shown in the `SLEEP --` slot. Encoders have no
push switch, so the long-press in the earlier plan isn't possible. Not saved: it starts off at every boot.

On expiry, do what `BTN_PLAY` does: `engine_pause()` and the 45 ms fade, with the reverb ringing out. `BTN_PLAY` resumes.
Main-loop only, a deadline checked against `fm1_ms`. It doesn't power off: the pond keeps running and the backlight stays
on. The FM-1's backlight is on a switchable pin (PA2), so turning it off could be added.

### 7. Key sets the drone root

Any of the 27 keys (`fm1_input_note_edges()`) moves the drone to the nearest scale degree, since a scale doesn't reach
every key, and re-roots at once. Press once and it's done: still a macro control, not note-by-note playing.
`engine_reroot()` walks from the current root, so this needs a new entry point that sets `home_degree` directly, kept
within `HOME_MIN_ST..HOME_MAX_ST`. The root isn't saved, and moods don't store it.

### 8. Hub submission

The full rules are in agents.md's [Distribution](agents.md#distribution-fm-1-firmware-hub) section.

- Verify the identity `FM-1_95N` against the Hub's live catalogue. It's still provisional.
- Complete `fmhub.json`, which has only `what`, `author` and `kinds`: add `play` (the Pages URL), `image` and `cheat` (a
  one-page PDF of the panel). `docs/screen.png` came with the Dinghy scaffold, so capture the pond for `image`.
- Test installing on real hardware both ways: from V15, and from another custom firmware.

## Shipped

### Sound

- **Voices:** a drone plus 3 colour voices, each with main and detuned partials (+1/256, about 7 cents). The main partial
  has harmonics 1-6 and the detuned one the fundamental and 2nd. The upper harmonics (4th-6th) follow the note's level,
  so notes brighten at the start and warm as they decay.
- **Colour notes:** each attacks (about 3 s), holds, releases (about 5 s to -60 dB) and retriggers on its own timer,
  picking up the current chord only when quiet (`envelope.h`, `engine.h`).
- **Drone:** an octave below the chord and about 15 dB under a full-level colour note.
- **Movement:** a slow pitch drift per voice, up to about 13.5 cents, and a slow swell in each note's level and in the
  colour low-pass ([soft focus](#2-soft-focus-lfo)). Colour voices have fixed stereo places.
- **Space:** a Schroeder reverb (`reverb.h`, 4 damped combs into 2 series feedback combs, T60 about 2.7 s), then the
  `ENC_KNOB2` low-pass on the final mix.

### Generative engine

No clock: every voice free-runs on a semi-random timer. The rules are pure, host-tested headers. `engine.h` returns
events that `impressionism.c` applies to the sound, `planing.h` holds the scales and chord logic, and `settings.h` holds
the saved settings.

- **Chord moves:** the chord re-planes on the density timer, and the drone re-roots on a much slower one, every 60-180 s.
- **Density** sets how often the chord moves, how soon each colour voice re-enters, and how long notes hold
  (`DENSITY_TABLE` in `planing.h`, `HOLD_TABLE` in `engine.h`):

| Density | Chord moves and re-entry | Note hold |
| ------- | ------------------------ | --------- |
| D0      | 8-20 s                   | 2.5-7 s   |
| D1      | 5-12 s                   | 2.5-6 s   |
| D2      | 3-7 s                    | 2-5 s     |
| D3      | 1.2-3.5 s                | 1.2-3 s   |

- **Pause:** no events are sent. A held note is cut, and a scale change during a pause is applied on resume. Resume picks
  the timers up where they stopped, and a voice with no wait left gets a new random one, so they don't all enter at once.
- **Seed:** mixes boot time and two ADC reads, so each power-on differs (confirmed on the FM-1). The engine's position
  isn't saved.

### Chord and scale expansion

- **Scales (6):** whole-tone, pentatonic, minor pentatonic, Lydian, Mixolydian, Dorian.
- **Chords (8), in semitones above the root:** maj7 +4,+7,+11; add9 +4,+7,+14; sus2 +2,+7,+12; sus4 +5,+7,+12; 6/9
  +4,+9,+14; quartal +5,+10,+15; 9no5 +4,+10,+14; aug9 +4,+8,+14.
- **Degree-fit rule:** a chord fits a scale degree when all three voices land on scale notes. Each move picks at random
  among those that fit, and every degree of every scale has at least one. Whole-tone uses only 9no5 and aug9.
- **Register limits** (`clamp_register`): the drone plays notes 24-48, chord roots stay in 36-67, and the top voice stays
  at note 82 or below.

### Panel mapping

| Control              | Does                                                              |
| -------------------- | ----------------------------------------------------------------- |
| `ENC_SELECT`         | scale, cycling through all 6 either way                           |
| `ENC_ALGORITHM`      | density, 0-3                                                      |
| `ENC_KNOB1`          | drift: how far each chord move or re-root steps, 0-3              |
| `ENC_KNOB2`          | colour: the low-pass on the final mix, 0 darkest to 31 fully open |
| `BTN_ARP`            | drone on/off, LED lit when on                                     |
| `BTN_PLAY`           | pause/resume: 45 ms fade, timers frozen. LED lit while generating |
| MASTER               | volume                                                            |
| The other 12 buttons | soft backlight only                                               |

**Status rows:** `PENTATONIC  DRONE ON  D0`, then `DRIFT 0  COLOUR 12`, then `VOICE --  SLEEP --` until items 4 and 6 fill
those in. The dev overlays `ISR_TIMING` and `SEED_SHOW` share the bottom row, so enable only one at a time.

**Saving:** the five settings persist across power-off (`settings_save`). A save waits for 2 s after the last change while
paused, or 30 s while playing, because the flash erase interrupts the sound.

### Screen

The screen is a visualization that matches the mood of the music, and it should read as Monet's _Water Lilies_: an
impressionist painting in blocky pixel art, not a dashboard. It isn't a control readout, so no control moves the pond.

- **Lilies:** the greens. Each pad stays in place, with a small random wander of up to 2 px.
- **Water:** everything else, a nearly continuous field with two slow crossing waves, not per-cell noise.
- **Pace:** slow and constant. Nothing flickers, jumps or speeds up.
- **Palette:** five colours, picked by eye from the painting (`MONET_*` in `scene.h`): `#3B668C` and `#5888A6` water,
  `#285959` and `#678C4D` leaves, `#8451A6` flower.

The music shows up as colour and presence, never speed:

| Source                    | Shows as                                                    |
| ------------------------- | ----------------------------------------------------------- |
| `ENC_KNOB2` (colour)      | water brightness, dark blue to lit blue-teal, no hue change |
| `ENC_ALGORITHM` (density) | how many pads bloom: 3/4 at D0 down to 1/8 at D3            |
| Chord template            | bloom shade: the one purple, dimmed per chord               |
| Nothing, always on        | the water's flow and the pads' wander                       |
| Nothing, by design        | `BTN_ARP`, `ENC_KNOB1` and every other control              |

### Browser emulator

Live at <https://darrenjaworski.github.io/fm-1-impressionism/>. It uses
[fm1-emu](https://github.com/joakimeriksson/fm1-emu), a generic FM-1 CPU emulator that boots an unmodified `.fwsc`, so
this firmware needs no web port. `.github/workflows/pages.yml` builds it at the pinned `FM1_EMU_REF`, adds the latest
`.fwsc` and this project's catalog, and publishes on every release or push to `site/**`. `FM1_EMU_REF` is updated by hand.

## Measurements

**Against the reference.** Host renders of `tests/scripts/long.txt` (3 minutes at default settings), measured with
`tools/measure_render.py`, which skips the first 15 s and has a `--selftest` against signals with known answers. The
targets come from the reference recording, a Debussy "Reverie" on a modular synth. The ear is the final judge; these
numbers guide the work.

| Metric                           | `5beb646` | `e4762d8` | Soft focus | Rebalanced | Target        |
| -------------------------------- | --------- | --------- | ---------- | ---------- | ------------- |
| Envelope range p95-p10 (1 s RMS) | 11.4 dB   | 11.0 dB   | 14.1 dB    | 21.4 dB    | 15+ dB        |
| Spectral centroid                | 193 Hz    | 169 Hz    | 169 Hz     | 407 Hz     | 350-500 Hz    |
| 20-100 Hz band                   | -3.6 dB   | -3.5 dB   | -3.5 dB    | -14.3 dB   | -15 to -25 dB |
| 1.6-3.2 kHz band                 | -39 dB    | -44 dB    | -47 dB     | -30 dB     | -25 to -35 dB |
| L/R correlation                  | 0.51      | 0.49      | 0.49       | 0.46       | 0.3-0.5       |
| Clipped samples                  | 0         | 0         | 0          | 0          | 0             |

`5beb646` is the commit the earlier table was taken on, re-measured with this script. The earlier figures for envelope
range (7.7 dB), centroid (236 Hz) and the 20-100 Hz band (-8 dB) couldn't be reproduced from that commit, so they came
from different definitions and aren't comparable. How the Target column was measured isn't recorded, so treat its
envelope, centroid and band targets as approximate until the reference is measured with this script. Bands are dB below
the total power. Between `5beb646` and `e4762d8` the centroid fell 24 Hz and the 1.6-3.2 kHz band fell 5 dB, which fits the register
limits holding notes lower. The soft-focus filter sweep lowered the 1.6-3.2 kHz band by another 3 dB, because it only dips. Overall level is not
a target: it went from -24.9 dBFS RMS (soft focus) to -29.4 dBFS (rebalanced).

**Note rate.** `tests/engine_test.c`, host only: 4 seeds x 6 scales x 4 drifts x 3 simulated hours per density, with a
voice counted as quiet 6 s after its note off. The D2 to D3 step is the smallest (19%, against 33-47%), because most of a
D3 voice's cycle is that release, not its hold.

| Density | Notes per minute |
| ------- | ---------------- |
| D0      | 9.55             |
| D1      | 14.01            |
| D2      | 18.61            |
| D3      | 22.14            |

**Audio ISR.** Worst block 1.3 ms of the 2.9 ms budget on the FM-1 (2026-10-31), up from 0.6 ms before harmonics and
stereo. Show it on screen with `ISR_TIMING=1`, and re-measure after any per-sample change.

## Not planned

- **Tempo or clock:** deliberate. The mission rules out a clock-quantized sequencer, and density and drift already cover
  "how often" and "how far". Syncing density to incoming MIDI clock would be a narrow exception, only if wanted later.
- **Played-note performance:** this is an instrument that drifts, so the keys are macro controls.
- **A reverb wet/dry knob:** the reverb is part of the sound, and damping is the only control considered.
