/* SPDX-License-Identifier: GPL-3.0-only */
/* fm-1-impressionism, forked from zvenson/dinghy (https://github.com/zvenson/dinghy): the smallest FM-1 firmware
 * to build your own on. Everything below this file (platform.c) is done: updates, the USB rescue, MIDI (USB, the
 * TRS jack), USB audio, keys, knobs, LEDs, the screen, flash storage; the FM-1 and the SMK-37.
 *
 * STATUS: v1 of the real engine (ROADMAP.md), replacing Dinghy's stock sine demo. A generative ambient instrument,
 * not a played one (CLAUDE.md's "Mission") -- the 27-key keyboard and MIDI in/out are intentionally unused here;
 * nothing listens to them. Four voices, fixed roles:
 *
 *   voice 0   the drone: a sustained root/pedal tone. Re-roots itself on its own slow, independent timer.
 *   voice 1-2 the chord's two lower colour tones above that root, from one of 8 templates (ROADMAP.md),
 *             each triggered individually when quiet, holding for a density-dependent time (engine.h), then releasing
 *   voice 3   the melody (melody.h): a short theme on the chord's top tone, which comes back changed, with long silences.
 *             Its notes come from its own timer, not from the quiet rule, and it attacks faster (about 80 ms)
 *
 * This file is the sound, the input and the screen. The decisions (which notes, when) are in engine.h, which returns
 * events that apply_events() turns into voice settings here.
 *
 * Panel (ROADMAP.md's agreed mapping):
 *   ENC_SELECT     scale: cycles through 6 (whole-tone, pentatonic, minor pentatonic, Lydian, Mixolydian, Dorian)
 *   ENC_ALGORITHM  density: how often the chord moves, how soon each colour voice re-enters, and how long its notes
 *                  hold (0 sparsest .. 3 densest)
 *   ENC_KNOB1      drift rate: how far each re-plane/re-root steps (0 smallest .. 3 widest)
 *   ENC_KNOB2      colour: a one-pole low-pass on the final mix (0 darkest .. 31 fully open)
 *   BTN_ARP        drone on/off
 *   BTN_PLAY       pause/resume: fades all voices out (~45 ms) and freezes every timer; resume restarts them where they
 *                  stopped, and a voice with no wait left gets a new random one
 *   all 14 buttons are softly backlit (dim glow layer); ARP and PLAY light fully for "on"
 *   MASTER         volume (platform.c; nothing to do here)
 * scale/density/drift/colour/drone-on persist across power-off (settings.h); the engine's position (its wander, the
 * chord) does not: it is reseeded from the boot time and ADC noise on every boot (ROADMAP.md).
 *
 * The screen is an impressionist pixel-art lily pond (ROADMAP.md): water brightness from ENC_KNOB2, lily blooms
 * coloured by the current chord template and shimmering with the water, a slow constant shimmer and a
 * small brownian drift of the pads that ignore the panel, and how many pads bloom from density (scene.h) -- plus a
 * status-text overlay below it (status.h: the scale name, drone state, density, and the knob values).
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "IMPRESSIONISM"                /* the rescue and the crash screen say this (docs/MAKE-IT-YOURS.md) */
static void app_idle(void) {}                   /* nothing to do while the screen's DMA runs: no MIDI in, in v1 */
#define LCD_IDLE app_idle
#define CV_MAX (240u * 120u)                     /* platform.c's own default is 240x64; the pond's canvas needs
                                                     the full 120 px it's drawn at (scene.h's POND_H). Costs
                                                     ~57.6 KB of the 336 KB pool, comfortably within budget. */
#include "platform.c"
#include "planing.h"                            /* scales, chord templates, the planing picker, the scheduler */
#include "settings.h"                           /* the seven saved settings, their defaults and checks */
#include "moods.h"                              /* the eight moods: factory settings, names, the dial */
#include "engine.h"                             /* the generative engine: what plays and when, as events */
#include "synth.h"                              /* sine, the phase step table, note_to_inc, pitch_drift */
#include "scene.h"                              /* the lily pond */
#include "status.h"                             /* the status text under the pond: scale, drone, density, knobs */
#include "reverb.h"                             /* Schroeder reverb: 4 parallel damped combs + 2 series feedback combs, T60 about 2.7 s */
#include "envelope.h"                           /* per-note attack and release for the colour voices */

#ifndef ISR_TIMING
#define ISR_TIMING 0                            /* 1: show the audio ISR's worst block time on screen (RULES.md: stay under 2.9 ms) */
#endif
#ifndef SEED_SHOW
#define SEED_SHOW 0                             /* 1: show the boot seed in hex on screen, to check it differs across power-ons.
                                                   Same line as ISR_TIMING, so not both at once */
#endif

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define NV 4u                                   /* 0 = drone, 1..2 = the chord's colour tones, 3 = the melody (MELODY_VOICE) */
#define MELODY_VOICE (ENGINE_MELODY_VOICE + 1u) /* voice[] index of the melody: engine voice 2 (engine.h) */
static struct {
    volatile uint32_t inc;                      /* phase step per frame: f * 2^32 / 44100 */
    volatile int32_t gate;                      /* target level, Q15-ish: 0 = released */
    volatile int32_t amp;                       /* the level now: glides to gate, no clicks; shared ISR/main */
    uint32_t ph;
    uint32_t ph_detune;                         /* the second, slightly sharp partial (see app_audio) */
    uint32_t lfo_ph;                            /* slow pitch drift: ~+-13.5 cents at full swing, once per block (see synth.h) */
} voice[NV];
/* each voice's place in the stereo field (Q15): the drone sits centre, the colour voices lean left, right and centre */
static const int32_t pan_l[NV] = { 32767, 32767, 12000, 22000 };
static const int32_t pan_r[NV] = { 32767, 12000, 32767, 22000 };
static const uint32_t lfo_inc[NV] = { 1744100u, 3240900u, 4736700u, 6731100u };  /* 0.07, 0.13, 0.19, 0.27 Hz */
#define LFO_AMP_DEPTH 24576                     /* each note's level dips by up to this (Q15, 0.75) of itself: 12 dB, never above unity */
#define LFO_FILTER_DEPTH 13107                  /* the colour low-pass's coefficient swings +-this (Q15, 0.4) of itself */
#define LFO_QUARTER 0x40000000u                 /* a quarter cycle: level follows the drift LFO a quarter late, so pitch and level don't move together */
static const uint32_t filter_lfo_inc = 2742546u;  /* 0.11 Hz, per block: 0.11 * 2^32 / (44100 / 256) */
static uint32_t filter_lfo_ph;                  /* the filter's own LFO phase (audio ISR only) */
static volatile uint32_t paused = 0u;          /* BTN_PLAY: 1 = paused (the ISR fades out), 0 = generating */
static env_t env[NV - 1u];                     /* colour voices: env[v] is voice[v + 1]. Each note attacks, holds, releases (envelope.h) */
#define HARM_FUND 13107                         /* the main partial's mix, Q15: fundamental 0.40, 2nd 0.35, 3rd 0.25 (sums to 1) */
#define HARM_2 11469
#define HARM_3 8192
#define HARM_4 8000                             /* 4th..6th partials, scaled by the note's brightness (see app_audio): 0.24, 0.16, 0.08 at full */
#define HARM_5 5200
#define HARM_6 2600
#define DET_FUND 13107                          /* the detuned partial's mix, Q15: fundamental 0.40, 2nd harmonic 0.60 (sums to 1). A pure */
#define DET_2 19661                             /* fundamental here held the centroid near 250 Hz whatever the main partial did */
#define DRONE_FULL 1500                         /* the pedal sits ~15 dB under a full colour note: at 4000 it held nearly all the power below 100 Hz */

/* ISR envelope glide: smooth the envelope toward the amp level per block (~5.8 ms) to avoid clicks.
   Applied only to the drone; colour voices are shaped by envelope.h's attack/release. */
#define ATTACK_STEP 8                           /* glide step when envelope level rises */
#define RELEASE_STEP 5                          /* glide step when envelope level falls */
#define MELODY_ATTACK_STEP 2368u                /* Q8 per block: 32640 / 2368 = 13.8 blocks, about 80 ms to full (a block is 5.8 ms).
                                                   The melody's notes are short, so they attack faster than the colour voices' ENV_ATTACK_STEP */
static volatile int32_t mel_level = 200;        /* the melody's level for the note now sounding (melody.h's 200 or 120, 0..255); set
                                                   by apply_events, read by the ISR. One writer: the main loop */

#ifdef DINGHY_HOST
static reverb_t reverb;                         /* host build: plain memory (the .pool section is ELF-only) */
#else
static reverb_t reverb __attribute__((section(".pool")));  /* ~15 KB of the .pool section (336 KB available) */
#endif
#if ISR_TIMING && !defined(DINGHY_HOST)
static volatile uint32_t isr_max_ticks;         /* worst app_audio time seen, in fm1_ticks() */
#endif

static volatile int32_t filter_k = 16384;        /* ENC_KNOB2: 1024 (dark) .. 32768 (fully open) */
static int32_t lp_l, lp_r;                       /* the one-pole filter's own state (audio ISR only) */

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t v, i;
    int32_t bright, k;
#if ISR_TIMING && !defined(DINGHY_HOST)
    uint32_t t_start = fm1_ticks();
#endif
    for (v = 0; v < NV; v++) {
        uint32_t ph = voice[v].ph, step;
        uint32_t ph_d, step_d;
        int32_t drift;
        voice[v].lfo_ph += lfo_inc[v];
        drift = pitch_drift(voice[v].inc, sine(voice[v].lfo_ph));
        if (v == MELODY_VOICE) drift >>= 2;     /* a quarter of the drift (about 3.4 cents, not 13.5): the line stays in tune with the chord */
        step = voice[v].inc + (uint32_t)drift;
        ph_d = voice[v].ph_detune;
        step_d = step + (step >> 8);  /* +1/256 = ~7 cents: a slow chorus shimmer, not a near-semitone clash */
        int32_t amp = voice[v].amp, g = voice[v].gate;
        if (paused) {                           /* BTN_PLAY: halve every block, ~45 ms to silence, not a click */
            amp >>= 1;
        } else if (v == 0) {                    /* the drone: a slow gate glide, once per block not per sample */
            if (amp < g) { amp += ATTACK_STEP; if (amp > g) amp = g; }
            else if (amp > g) { amp -= RELEASE_STEP; if (amp < g) amp = g; }
        } else {
            env_tick(&env[v - 1u], v == MELODY_VOICE ? MELODY_ATTACK_STEP : ENV_ATTACK_STEP);
            amp = env_to_amp(&env[v - 1u]);
        }
        voice[v].amp = amp;
        if (v == MELODY_VOICE) amp = (amp * mel_level) >> 8;   /* the note's level, 200/256 or 120/256 (melody.h) */
        if (!amp) continue;                     /* silent: costs nothing (g == 0 too, once amp has caught up) */
        if (v != MELODY_VOICE)                  /* soft focus: this block's level only, voice[v].amp keeps the glide. Not on the melody: a note must not sink at random */
            amp = (amp * amp_swell(sine(voice[v].lfo_ph + LFO_QUARTER), LFO_AMP_DEPTH)) >> 15;
        bright = amp << 2;                      /* upper partials follow the note's level: bright at the start, warmer as it decays */
        if (bright > 32767) bright = 32767;
        for (i = 0; i < n; i++) {
            int32_t h = (sine(ph) * HARM_FUND + sine(ph << 1) * HARM_2 + sine(ph + (ph << 1)) * HARM_3) >> 15;
            int32_t up = (sine(ph << 2) * HARM_4 + sine((ph << 2) + ph) * HARM_5 + sine(ph * 6u) * HARM_6) >> 15;
            int32_t s_main = ((h + ((up * bright) >> 15)) * amp) >> 15, s_det = (((sine(ph_d) * DET_FUND + sine(ph_d << 1) * DET_2) >> 15) * amp) >> 15;
            ph += step;
            ph_d += step_d;
            out[2u * i] += (((3 * s_main + s_det) >> 2) * pan_l[v]) >> 15;     /* L leans on the main partial... */
            out[2u * i + 1u] += (((s_main + 3 * s_det) >> 2) * pan_r[v]) >> 15; /* ...R on the detuned one: width from the chorus */
        }
        voice[v].ph = ph;
        voice[v].ph_detune = ph_d;
    }
    /* the space: Schroeder reverb on the mono sum (4 damped combs + 2 feedback combs, T60 about 2.7 s) */
    for (i = 0; i < n; i++) {
        int16_t wl, wr;
        int32_t mono = (out[2u * i] + out[2u * i + 1u]) >> 1;
        reverb_step(&reverb, mono, &wl, &wr);
        out[2u * i] += (int32_t)wl;
        out[2u * i + 1u] += (int32_t)wr;
    }
    /* the "colour" knob: a one-pole low-pass on the finished mix, docs/MAKE-IT-YOURS.md's own suggested formula,
       with its cutoff swaying around the knob's setting */
    filter_lfo_ph += filter_lfo_inc;
    k = filter_sweep(filter_k, sine(filter_lfo_ph), LFO_FILTER_DEPTH);
    for (i = 0; i < n; i++) {
        lp_l += (out[2u * i] - lp_l) * k >> 15;
        lp_r += (out[2u * i + 1u] - lp_r) * k >> 15;
        out[2u * i] = lp_l > 32767 ? 32767 : lp_l < -32767 ? -32767 : lp_l;           /* dry + wet can pass full scale */
        out[2u * i + 1u] = lp_r > 32767 ? 32767 : lp_r < -32767 ? -32767 : lp_r;
    }
#if ISR_TIMING && !defined(DINGHY_HOST)
    {
        uint32_t dt = (uint32_t)(fm1_ticks() - t_start);
        if (dt > isr_max_ticks) isr_max_ticks = dt;
    }
#endif
}

/* ---- the generative engine (engine.h) on the main loop: its events, applied to the sound here */
static engine_t eng;                            /* what plays and when: the drone, the chord, the timers */
static engine_event_t events[ENGINE_MAX_EVENTS];

/* the engine's events, applied to the sound: the drone's pitch, a colour voice's pitch and gate */
static void apply_events(uint32_t n)
{
    uint32_t i, v;
    for (i = 0; i < n; i++) {
        v = events[i].voice;
        if (events[i].kind == EV_DRONE) {
            voice[0].inc = note_to_inc(events[i].note);
        } else if (events[i].kind == EV_NOTE_ON) {
            voice[v + 1u].inc = note_to_inc(events[i].note);   /* no phase reset: a melody note slurs from the last one, no click */
            env_gate_on(&env[v]);
            if (v == ENGINE_MELODY_VOICE) mel_level = events[i].level;
        } else {
            env_gate_off(&env[v]);
        }
    }
}

/* bit v is set when colour voice v's envelope is quiet, so the engine may give it a new note */
static uint32_t quiet_mask(void)
{
    uint32_t v, m = 0u;
    for (v = 0; v < ENGINE_VOICES; v++)
        if (env_is_quiet(&env[v])) m |= 1u << v;
    return m;
}

/* ---- settings (what survives a power-off, settings.h) */
static settings_t set;

/* ---- moods (moods.h): each mood's blob is one settings_t in flash, read once at boot into mood_store. A mood whose bit
   in mood_saved_mask is clear has no valid blob and uses its factory settings. */
typedef char mood_count_matches_blobs[MOOD_COUNT == NBLOB ? 1 : -1];
static settings_t mood_store[MOOD_COUNT];
static uint32_t mood_saved_mask;

static int32_t color_to_filter_k(uint32_t color) { return 1024 + (int32_t)color * 1024; }   /* 0..31 -> 1024..32768 */

/* ---- the screen: a lily pond (scene.h), plus a small status-text overlay below it */
#define C_ACC RGB(180, 140, 255)
#define C_OFF RGB(90, 90, 90)
#define POND_Y 58                               /* screen y where the pond's canvas strip gets blitted */
#define POND_TICK_MS 300u                       /* the pond's clock: one phase tick per 300 ms of fm1_ms, and the
                                                   redraw tick (next_draw_ms) is the same 300 ms */

/* the pond's motion comes from the clock (phase = fm1_ms / POND_TICK_MS), not from a count of redraws: a state
   change that redraws early doesn't speed the water up or move the pads faster */
static void draw_pond(void)
{
    uint32_t x, y, phase = fm1_ms / POND_TICK_MS;
    rgb_t water = color_to_water_rgb(set.color), bloom = chord_templ_to_bloom_rgb(eng.chord_templ);
    cv_begin(POND_W * CELL_PX, POND_H * CELL_PX, C_BLACK);
    for (y = 0; y < POND_H; y++)                  /* the water: one continuous field of two slow crossing waves */
        for (x = 0; x < POND_W; x++) {
            rgb_t c = shimmer_rgb(water, water_shimmer(x, y, phase));   /* shimmer 0..7 */
            cv_rect((int32_t)(x * CELL_PX), (int32_t)(y * CELL_PX), CELL_PX, CELL_PX, RGB(c.r, c.g, c.b));
        }
    for (y = 0; y < POND_H; y++)                  /* the pads on top: nearly stationary, each with a tiny random wander */
        for (x = 0; x < POND_W; x++) {
            rgb_t c;
            if (cell_kind(x, y) != CELL_PAD) continue;
            c = pad_blooms(x, y, set.density) ? shimmer_rgb(bloom, tri8(x + y + phase / 4u)) : pad_leaf_rgb(x, y);   /* leaves: scene.h's two greens */
            cv_rect(pad_pos(x, y, phase, 0u), pad_pos(x, y, phase, 1u), CELL_PX, CELL_PX, RGB(c.r, c.g, c.b));
        }
    cv_blit(0, POND_Y);
}

#define TITLE_Y 18
#define TITLE_NAME_MS 3000u                     /* a mood's name replaces the title for this long, then the title returns */
static void draw_title(const char *text)        /* draw_text_box clears its own box, so a new text covers the old one */
{
    draw_text_box(0, TITLE_Y, 240, &FONT_L, text, C_ACC, 1);
}

static void draw_chrome(void)                   /* once, at startup only: the black screen and the title never
                                                     change again, so they never belong in a function called on
                                                     every redraw tick (docs/RULES.md: "draw only what changed" --
                                                     an unconditional full-screen lcd_fill here was flashing the
                                                     whole screen black on every ~300ms pond tick, visible as a
                                                     ~3 Hz stutter on real hardware) */
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_title(APP_NAME);
}

static void draw_status(void)                   /* only on a real settings/chord change, not every pond tick */
{
    char s[32], k[32], b[32];
    status_top(s, set.scale, set.drone_on, set.density);                 /* the scale, drone and density */
    draw_text_box(0, POND_Y + POND_H * CELL_PX + 6u, 240, &FONT_S, s, C_WHITE, 1);
    status_knobs(k, set.drift, set.color);                               /* ENC_KNOB1 drift, ENC_KNOB2 colour */
    draw_text_box(0, POND_Y + POND_H * CELL_PX + 26u, 240, &FONT_S, k, C_OFF, 1);
    status_bottom(b, set.voice_lead);                                    /* ENC_KNOB3 voice leading, and the sleep slot */
    draw_text_box(0, POND_Y + POND_H * CELL_PX + 46u, 240, &FONT_S, b, C_OFF, 1);
}

#if ISR_TIMING && !defined(DINGHY_HOST)
static void draw_isr_time(void)                 /* worst app_audio block so far, in tenths of a ms (budget: 5.8 ms, aim < 2.9) */
{
    char s[16];
    uint32_t t = isr_max_ticks / (100u * FM1_TICKS_PER_US), i = 0;   /* ticks -> 0.1 ms */
    s[i++] = 'I'; s[i++] = 'S'; s[i++] = 'R'; s[i++] = ' ';
    if (t >= 100u) s[i++] = (char)('0' + t / 100u % 10u);
    s[i++] = (char)('0' + t / 10u % 10u); s[i++] = '.'; s[i++] = (char)('0' + t % 10u);
    s[i++] = ' '; s[i++] = 'M'; s[i++] = 'S'; s[i] = 0;
    draw_text_box(0, POND_Y + POND_H * CELL_PX + 46u, 240, &FONT_S, s, C_OFF, 1);
}
#else
static void draw_isr_time(void) {}
#endif

#if SEED_SHOW && !defined(DINGHY_HOST)
static uint32_t boot_seed;                       /* the seed app_main gave the engine, shown by draw_seed */
static void draw_seed(void)                     /* the boot seed in hex, on draw_isr_time's line */
{
    static const char HEX[] = "0123456789ABCDEF";
    char s[14];
    uint32_t k;
    s[0] = 'S'; s[1] = 'E'; s[2] = 'E'; s[3] = 'D'; s[4] = ' ';
    for (k = 0; k < 8u; k++) s[5u + k] = HEX[boot_seed >> (28u - 4u * k) & 15u];
    s[13] = 0;
    draw_text_box(0, POND_Y + POND_H * CELL_PX + 46u, 240, &FONT_S, s, C_OFF, 1);
}
#else
static void draw_seed(void) {}
#endif

static void app_main(void)
{
    uint32_t t_change = 0, dirty = 0, scale_drawn = ~0u, state_drawn, next_draw_ms, b, rel, st, n, v;
    uint32_t title_until = 0u;                  /* when the mood's name hands the title back (0: the title is showing) */
    uint32_t save_pending = 0u, save_mood = 0u, t_save = 0u;   /* BTN_SAVE: a write waiting for a quiet moment */
    settings_t save_snap;                       /* what BTN_SAVE saves: the settings as they were at the press */
    uint32_t seed = 0x9E3779B9u;                /* host: a fixed seed (never 0) */
    int32_t e;

    if (settings_load(&set, sizeof set) || !settings_valid(&set))
        settings_set_defaults(&set);
#ifndef DINGHY_HOST
    {   /* boot seed: the ms counter, the CPU tick count and two ADC reads (battery, master pot) are mixed together,
           so the start depends on all of them. A timed-out read (-1) is skipped. cell_hash spreads the bits; 0 is
           never a seed (engine_init maps it to 1). Checked on the FM-1 with SEED_SHOW: it differs across power-ons. */
        int32_t a;
        seed = cell_hash(fm1_ms, fm1_ticks());
        if ((a = fm1_adc_read(FM1_ADC_BATT)) >= 0) seed = cell_hash(seed, (uint32_t)a);
        if ((a = fm1_adc_read(FM1_ADC_MASTER)) >= 0) seed = cell_hash(seed, (uint32_t)a);
#if SEED_SHOW
        boot_seed = seed;
#endif
    }
#endif
    for (v = 0; v < MOOD_COUNT; v++)            /* the moods saved so far; a blob of another size or mood reads as empty */
        if (!blob_load(v, &mood_store[v], sizeof mood_store[v]) && mood_blob_ok(&mood_store[v], v))
            mood_saved_mask |= 1u << v;
    filter_k = color_to_filter_k(set.color);
    n = engine_init(&eng, seed, &set, fm1_ms, events);   /* a real root and chord already in place, the first waits */
    apply_events(n);
    draw_chrome();
    draw_pond();
    draw_status();
    scale_drawn = set.scale;
    state_drawn = settings_state_word(&set, eng.chord_templ);
    next_draw_ms = fm1_ms + POND_TICK_MS;                 /* the pond's shimmer redraws on its own slow tick too */

    {   /* backlight every button (the FM-1's 14 are ids 0..13; a no-op on the SMK-37). The glow is its own dim layer
           and an LED takes the brightest layer (hal/fm1_input.h), so BTN_ARP/BTN_PLAY's lit state still reads
           clearly on top of it, and "off" is the dim glow rather than dark. */
        uint32_t id;
        for (id = 0; id < 14u; id++) fm1_led_key_glow(id, 1);
    }

    for (;;) {
        platform_poll();

        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            set.scale = (uint8_t)scale_step(set.scale, e);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            set.density = knob_apply(set.density, e, 3u);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            set.drift = knob_apply(set.drift, e, 3u);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            set.color = knob_apply(set.color, e, 31u);
            filter_k = color_to_filter_k(set.color);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {
            set.voice_lead = knob_apply(set.voice_lead, e, 3u);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {  /* the mood dial: loads a mood into the live settings, doesn't save */
            v = mood_step(set.mood, e);
            if ((mood_saved_mask >> v) & 1u) settings_copy(&set, &mood_store[v]);
            else mood_factory(&set, v);
            filter_k = color_to_filter_k(set.color);   /* the scale change, if any, is picked up below like ENC_SELECT's */
            draw_title(((mood_saved_mask >> v) & 1u) ? mood_saved_name(v) : mood_piece_name(v));
            title_until = fm1_ms + TITLE_NAME_MS;
            dirty = 1; t_change = fm1_ms;
        }
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_SAVE)) {                  /* keep the settings as they are now; the write waits for a quiet moment */
            settings_copy(&save_snap, &set);
            save_mood = set.mood;
            save_pending = 1u;
            t_save = fm1_ms;
        }
        fm1_led_key(BTN_SAVE, save_pending != 0u);   /* lit while a save is waiting, out once it is written */
        if (b & (1u << BTN_ARP)) { set.drone_on ^= 1u; dirty = 1; t_change = fm1_ms; }
        fm1_led_key(BTN_ARP, set.drone_on);
        if (b & (1u << BTN_PLAY)) {                  /* pause/resume: freezes generation; the sound fades out (app_audio) */
            paused ^= 1u;
            if (paused) {
                engine_pause(&eng, fm1_ms);
            } else {
                n = engine_resume(&eng, fm1_ms, &set, events);
                apply_events(n);
            }
        }
        fm1_led_key(BTN_PLAY, !paused);              /* lit while generating, off while paused */

        if (scale_drawn != set.scale) {          /* the scale changed: every voice due now, re-root and re-plane at once */
            scale_drawn = set.scale;
            n = engine_scale_changed(&eng, fm1_ms, &set, events);
            apply_events(n);
        }

        n = engine_tick(&eng, fm1_ms, &set, quiet_mask(), events);   /* the drone, the chord and each colour voice's notes */
        apply_events(n);

        /* voice gates: drone controlled by setting, colour voices managed by the engine's notes */
        voice[0].gate = (set.drone_on && !paused) ? DRONE_FULL : 0;

        /* paused: the envelopes are cleared, so no note comes back on resume (the sound itself fades out in app_audio) */
        if (paused) {
            for (v = 0; v < ENGINE_VOICES; v++) env_init(&env[v]);
        }

        st = settings_state_word(&set, eng.chord_templ);
        if (st != state_drawn) {                 /* a real settings/chord change: redraw both right away */
            state_drawn = st;
            draw_pond();
            draw_status();
            next_draw_ms = fm1_ms + POND_TICK_MS;
        } else if ((int32_t)(fm1_ms - next_draw_ms) >= 0) {   /* just the pond's own slow shimmer tick --
                                                                   the title and status text never change here */
            draw_pond();
            draw_isr_time();
            draw_seed();
            next_draw_ms = fm1_ms + POND_TICK_MS;
        }

        /* settings save: prefer a quiet moment (no audible glitch from the erase, docs/RULES.md). While paused the sound
           has faded out, so save then; otherwise wait for 30 s without a change, since this is an ambient instrument
           that may rarely go fully silent -- don't wait forever for one. Never sooner than 2 s after the last change. */
        if (save_pending && save_due(fm1_ms, t_save, paused)) {   /* the mood's blob: its own sector, so no other mood is rewritten */
            if (blob_save(save_mood, &save_snap, sizeof save_snap) == 0) {
                settings_copy(&mood_store[save_mood], &save_snap);
                mood_saved_mask |= 1u << save_mood;
                draw_title(mood_saved_name(save_mood));   /* it's now saved over: it reads MOOD n from here on */
                title_until = fm1_ms + TITLE_NAME_MS;
            }
            save_pending = 0u;
            if (dirty) { settings_save(&set, sizeof set); dirty = 0; }   /* one quiet moment for both writes */
        }
        if (dirty && save_due(fm1_ms, t_change, paused)) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
        if (title_until && (int32_t)(fm1_ms - title_until) >= 0) {   /* the name has been up long enough */
            draw_title(APP_NAME);
            title_until = 0u;
        }
    }
}
