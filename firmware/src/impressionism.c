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
 *   voice 1-3 the chord's three colour tones above that root, from a template (maj7 or add9, ROADMAP.md),
 *             re-planed (a new template + a scale-degree transposition) on its own faster timer
 *
 * Panel (ROADMAP.md's agreed mapping):
 *   ENC_SELECT     scale: whole-tone / pentatonic (toggles on any turn)
 *   ENC_ALGORITHM  density: how often the chord re-planes (0 sparsest .. 3 densest)
 *   ENC_KNOB1      drift rate: how far each re-plane/re-root steps (0 smallest .. 3 widest)
 *   ENC_KNOB2      colour: a one-pole low-pass on the final mix (0 darkest .. 31 fully open)
 *   BTN_ARP        drone on/off
 *   BTN_PLAY       pause/resume generation (the current chord keeps sustaining; timers resume where they stopped)
 *   all 14 buttons are softly backlit (dim glow layer); ARP and PLAY light fully for "on"
 *   MASTER         volume (platform.c; nothing to do here)
 * scale/density/drift/colour/drone-on persist across power-off; the current wander position does not (it
 * re-randomizes fresh each boot -- only the knob settings are meant to survive, ROADMAP.md).
 *
 * The screen is a reactive impressionist pixel-art lily pond (ROADMAP.md): water brightness from ENC_KNOB2,
 * lily blooms coloured by the current chord template (and rippling with the water), water shimmer speed from
 * drift rate, how many pads look "active" from density, and overall brightness from drone on/off -- plus a small status-text overlay below it
 * (the scale name, drone state, D/R).
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
#include "scene.h"                              /* the reactive pixel-art lily pond */
#include "diffuse.h"                            /* the cheap multi-tap diffuser (dreamier space) */

#ifndef ISR_TIMING
#define ISR_TIMING 0                            /* 1: show the audio ISR's worst block time on screen (RULES.md: stay under 2.9 ms) */
#endif

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define NV 4u                                   /* 0 = drone, 1..3 = the chord's colour tones */
static struct {
    volatile uint32_t inc;                      /* phase step per frame: f * 2^32 / 44100 */
    volatile int32_t gate;                      /* target level, Q15-ish: 0 = released */
    int32_t amp;                                /* the level now: glides to gate, no clicks */
    uint32_t ph;
    uint32_t ph_detune;                         /* the second, slightly sharp partial (see app_audio) */
} voice[NV];
#define GATE_FULL 8000                          /* 4 voices at this level sum to 32000, under full scale */

/* attack/release glide once per audio block (docs/RULES.md: envelopes need not run every sample), ~5.8 ms a
   block at 256 frames/44.1 kHz -- sized for a few seconds each (soft-focus, CLAUDE.md's Mission), tune by ear
   once this is heard on real hardware. Reduced for dreamier character: slower, more sustain. */
#define ATTACK_STEP 8                           /* ~5.8 s, 0 -> GATE_FULL (slower fade-in) */
#define RELEASE_STEP 5                          /* ~9.4 s, GATE_FULL -> 0 (slower fade-out) */

#ifdef DINGHY_HOST
static int16_t diff_buf[DIFF_LEN];              /* host build: plain memory (the .pool section is ELF-only, see gfx.c) */
#else
static int16_t diff_buf[DIFF_LEN] __attribute__((section(".pool")));
#endif
static uint32_t diff_pos;                       /* audio ISR only */
#if ISR_TIMING && !defined(DINGHY_HOST)
static volatile uint32_t isr_max_ticks;         /* worst app_audio time seen, in fm1_ticks() */
#endif

static volatile int32_t filter_k = 16384;        /* ENC_KNOB2: 1024 (dark) .. 32768 (fully open) */
static int32_t lp_l, lp_r;                       /* the one-pole filter's own state (audio ISR only) */

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;               /* 4x(1-|x|), Q15 */
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15; /* + 0.225 (y|y| - y) */
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t v, i;
#if ISR_TIMING && !defined(DINGHY_HOST)
    uint32_t t_start = fm1_ticks();
#endif
    for (v = 0; v < NV; v++) {
        uint32_t ph = voice[v].ph, step = voice[v].inc;
        uint32_t ph_d = voice[v].ph_detune, step_d = step + (step >> 8);  /* +1/256 = ~7 cents: a slow chorus shimmer, not a near-semitone clash */
        int32_t amp = voice[v].amp, g = voice[v].gate;
        if (amp < g) { amp += ATTACK_STEP; if (amp > g) amp = g; }        /* once per block, not per sample */
        else if (amp > g) { amp -= RELEASE_STEP; if (amp < g) amp = g; }
        voice[v].amp = amp;
        if (!amp) continue;                     /* silent: costs nothing (g == 0 too, once amp has caught up) */
        for (i = 0; i < n; i++) {
            int32_t s_main = (sine(ph) * amp) >> 15, s_det = (sine(ph_d) * amp) >> 15;
            ph += step;
            ph_d += step_d;
            out[2u * i] += (3 * s_main + s_det) >> 2;         /* L leans on the main partial... */
            out[2u * i + 1u] += (s_main + 3 * s_det) >> 2;    /* ...R on the detuned one: width from the chorus */
        }
        voice[v].ph = ph;
        voice[v].ph_detune = ph_d;
    }
    /* the space: a feed-forward multi-tap diffuser on the mono sum, returned into L and R (diffuse.h) */
    for (i = 0; i < n; i++) {
        int32_t wl, wr;
        diffuse_step(diff_buf, &diff_pos, (out[2u * i] + out[2u * i + 1u]) >> 1, &wl, &wr);
        out[2u * i] += wl;
        out[2u * i + 1u] += wr;
    }
    /* the "colour" knob: a one-pole low-pass on the finished mix, docs/MAKE-IT-YOURS.md's own suggested formula */
    for (i = 0; i < n; i++) {
        lp_l += (out[2u * i] - lp_l) * filter_k >> 15;
        lp_r += (out[2u * i + 1u] - lp_r) * filter_k >> 15;
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

/* ---- the generative engine (main loop): plans the next chord/root and sets the voices' inc/gate */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
#define ROOT_BASE_NOTE 48u                      /* C3-ish: a sensible register for a drone, tune by ear later */
#define DEGREE_MIN (-12)
#define DEGREE_MAX 12                            /* keeps the random walk in a musical register, not runaway */

static uint32_t note_to_inc(int32_t note)
{
    if (note < 0) note = 0;
    if (note > 120) note = 120;                 /* the table's reach (INC_C9 >> 10..0) */
    return INC_C9[(uint32_t)note % 12u] >> (10u - (uint32_t)note / 12u);
}
static int32_t clamp_degree(int32_t d) { return d < DEGREE_MIN ? DEGREE_MIN : d > DEGREE_MAX ? DEGREE_MAX : d; }

static uint32_t engine_rng = 0x9E3779B9u;         /* never 0 (planing.h's xorshift32) */
static int32_t home_degree, plane_degree;        /* the drone's root, and the chord's offset above it */
static uint32_t chord_templ;
static uint32_t next_plane_ms, next_root_ms;
static uint32_t paused = 0u;                     /* pause/resume toggle (BTN_PLAY): 1 = paused, 0 = generating */

static void retune_voices(uint32_t scale)
{
    int32_t root_note = (int32_t)ROOT_BASE_NOTE + scale_degree_to_semitone(scale, home_degree);
    int32_t chord_note = (int32_t)ROOT_BASE_NOTE + scale_degree_to_semitone(scale, home_degree + plane_degree);
    uint32_t v;
    voice[0].inc = note_to_inc(root_note);
    for (v = 1; v < NV; v++)
        voice[v].inc = note_to_inc(chord_note + chord_voice_semitone(chord_templ, v - 1u));
}

/* voice 1-3: re-plane on the density timer (ENC_ALGORITHM) */
static void replane(uint32_t scale, uint32_t density, uint32_t drift)
{
    plan_t p = plan_next(scale, plane_degree, drift, &engine_rng);
    plane_degree = clamp_degree(p.degree);
    chord_templ = p.templ;
    retune_voices(scale);
    next_plane_ms = fm1_ms + next_trigger_ticks(density, &engine_rng);
}

/* voice 0: re-roots on its own slower, independent timer (not density -- ROADMAP.md says density is "how often
   the 3 voices retrigger", the drone is deliberately not on that clock), using the same drift_rate for its step
   size and resetting the chord back onto the new root when it moves (flagged design choice, see ROADMAP.md) */
static void reroot(uint32_t scale, uint32_t drift)
{
    plan_t p = plan_next(scale, home_degree, drift, &engine_rng);
    home_degree = clamp_degree(p.degree);
    plane_degree = 0;
    retune_voices(scale);
    next_root_ms = fm1_ms + 60000u + rng_next(&engine_rng) % 120001u;   /* 60-180 s: much slower than any plane rate */
}

/* ---- settings (what survives a power-off, ROADMAP.md) */
static struct { uint8_t scale, density, drift, color, drone_on; } set;
static void set_defaults(void)                  /* dreamier: sparse, slow, darker. Field-wise: no struct copy, so no memcpy call */
{
    set.scale = SCALE_PENTATONIC; set.density = 0; set.drift = 0; set.color = 12; set.drone_on = 1;
}

static int32_t color_to_filter_k(uint32_t color) { return 1024 + (int32_t)color * 1024; }   /* 0..31 -> 1024..32768 */

static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].amp) return 0;
    return 1;
}

/* ---- the screen: a reactive pixel-art lily pond (scene.h), plus a small status-text overlay below it */
#define C_ACC RGB(180, 140, 255)
#define C_OFF RGB(90, 90, 90)
static const char *const SCALE_NAME[SCALE_COUNT] = {"WHOLE-TONE", "PENTATONIC"};
#define POND_Y 58                               /* screen y where the pond's canvas strip gets blitted */
static uint32_t pond_total_pads, ripple_phase;   /* the layout is fixed (scene.h); counted once, not per draw */

static void init_pond(void)
{
    uint32_t x, y;
    pond_total_pads = 0;
    for (y = 0; y < POND_H; y++)
        for (x = 0; x < POND_W; x++)
            if (cell_kind(x, y) == CELL_PAD) pond_total_pads++;
}

static void draw_pond(void)
{
    uint32_t x, y, pad_i = 0, active_n = active_cell_count(set.density, pond_total_pads);
    uint32_t dim = set.drone_on ? 100u : 65u;    /* drone off: a cooler, dimmer pond (ROADMAP.md) */
    rgb_t water = color_to_water_rgb(set.color), bloom = chord_templ_to_bloom_rgb(chord_templ);
    cv_begin(240, POND_H * 10u, C_BLACK);
    for (y = 0; y < POND_H; y++)
        for (x = 0; x < POND_W; x++) {
            uint32_t shimmer = (cell_hash(x, y) + ripple_phase) % 8u;   /* 0..7: a travelling shimmer */
            rgb_t c;
            if (cell_kind(x, y) == CELL_PAD) {
                c = pad_i < active_n ? shimmer_rgb(bloom, shimmer) : pad_leaf_rgb(x, y);   /* leaves: scene.h's two greens */
                pad_i++;
            } else {
                c = shimmer_rgb(water, shimmer);
            }
            cv_rect((int32_t)(x * 10u), (int32_t)(y * 10u), 10, 10,
                    RGB((uint32_t)c.r * dim / 100u, (uint32_t)c.g * dim / 100u, (uint32_t)c.b * dim / 100u));
        }
    cv_blit(0, POND_Y);
    ripple_phase += ripple_step(set.drift);
}

static void draw_chrome(void)                   /* once, at startup only: the black screen and the title never
                                                     change again, so they never belong in a function called on
                                                     every redraw tick (docs/RULES.md: "draw only what changed" --
                                                     an unconditional full-screen lcd_fill here was flashing the
                                                     whole screen black on every ~300ms pond tick, visible as a
                                                     ~3 Hz stutter on real hardware) */
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
}

static void draw_status(void)                   /* only on a real settings/chord change, not every pond tick */
{
    char s[32];
    uint32_t i = 0;
    draw_text_box(0, POND_Y + POND_H * 10u + 6u, 240, &FONT_S, SCALE_NAME[set.scale], C_WHITE, 1);
    {
        const char *p = set.drone_on ? "DRONE ON " : "DRONE OFF";
        while (*p) s[i++] = *p++;
        s[i++] = ' '; s[i++] = ' ';
        s[i++] = 'D'; s[i++] = (char)('0' + set.density);   /* density */
        s[i++] = ' '; s[i++] = ' ';
        s[i++] = 'R'; s[i++] = (char)('0' + set.drift);     /* drift rate */
        s[i] = 0;
    }
    draw_text_box(0, POND_Y + POND_H * 10u + 26u, 240, &FONT_S, s, C_OFF, 1);
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
    draw_text_box(0, POND_Y + POND_H * 10u + 46u, 240, &FONT_S, s, C_OFF, 1);
}
#else
static void draw_isr_time(void) {}
#endif

static void app_main(void)
{
    uint32_t t_change = 0, dirty = 0, scale_drawn = ~0u, state_drawn = ~0u, next_draw_ms, b, rel, st;
    int32_t e, left_plane = 0, left_root = 0;

    if (settings_load(&set, sizeof set) || set.scale >= SCALE_COUNT || set.density > 3u || set.drift > 3u ||
        set.color > 31u || set.drone_on > 1u)
        set_defaults();
    filter_k = color_to_filter_k(set.color);
    init_pond();                                  /* the pond's layout is fixed; count its pads once */
    reroot(set.scale, set.drift);                /* start with a real root and chord already in place */
    replane(set.scale, set.density, set.drift);
    draw_chrome();
    draw_pond();
    draw_status();
    scale_drawn = set.scale;
    state_drawn = set.density | set.drift << 2u | set.drone_on << 4u | set.color << 5u | chord_templ << 10u |
                  set.scale << 11u;
    next_draw_ms = fm1_ms + 300u;                 /* the pond's shimmer redraws on its own slow tick too */

    {   /* backlight every button (the FM-1's 14 are ids 0..13; a no-op on the SMK-37). The glow is its own dim layer
           and an LED takes the brightest layer (hal/fm1_input.h), so BTN_ARP/BTN_PLAY's lit state still reads
           clearly on top of it, and "off" is the dim glow rather than dark. */
        uint32_t id;
        for (id = 0; id < 14u; id++) fm1_led_key_glow(id, 1);
    }

    for (;;) {
        platform_poll();

        if ((e = fm1_enc_take(ENC_SELECT)) != 0) { set.scale ^= 1u; dirty = 1; t_change = fm1_ms; }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t d = (int32_t)set.density + e;
            set.density = (uint8_t)(d < 0 ? 0 : d > 3 ? 3 : d);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t d = (int32_t)set.drift + e;
            set.drift = (uint8_t)(d < 0 ? 0 : d > 3 ? 3 : d);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            int32_t c = (int32_t)set.color + e;
            set.color = (uint8_t)(c < 0 ? 0 : c > 31 ? 31 : c);
            filter_k = color_to_filter_k(set.color);
            dirty = 1; t_change = fm1_ms;
        }
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_ARP)) { set.drone_on ^= 1u; dirty = 1; t_change = fm1_ms; }
        fm1_led_key(BTN_ARP, set.drone_on);
        if (b & (1u << BTN_PLAY)) {                  /* pause/resume: freezes generation, the current chord sustains */
            paused ^= 1u;
            if (paused) {                            /* remember how long each timer had left, so resuming doesn't */
                left_plane = (int32_t)(next_plane_ms - fm1_ms);   /* fire both overdue timers at once */
                left_root = (int32_t)(next_root_ms - fm1_ms);
            } else {
                next_plane_ms = fm1_ms + (uint32_t)(left_plane > 0 ? left_plane : 0);
                next_root_ms = fm1_ms + (uint32_t)(left_root > 0 ? left_root : 0);
            }
        }
        fm1_led_key(BTN_PLAY, !paused);              /* lit while generating, the dim glow while paused */

        if (scale_drawn != set.scale) {          /* the scale changed: re-root and re-plane right away, since a
                                                     degree means a different interval in a different scale */
            home_degree = 0; plane_degree = 0;
            reroot(set.scale, set.drift);
            replane(set.scale, set.density, set.drift);
            scale_drawn = set.scale;
        }
        if (!paused && (int32_t)(fm1_ms - next_root_ms) >= 0) reroot(set.scale, set.drift);
        if (!paused && (int32_t)(fm1_ms - next_plane_ms) >= 0) replane(set.scale, set.density, set.drift);
        voice[0].gate = (set.drone_on && !paused) ? GATE_FULL : 0;
        {
            uint32_t v;
            for (v = 1; v < NV; v++) voice[v].gate = paused ? 0 : GATE_FULL;
        }

        st = set.density | set.drift << 2u | set.drone_on << 4u | set.color << 5u | chord_templ << 10u |
             set.scale << 11u;
        if (st != state_drawn) {                 /* a real settings/chord change: redraw both right away */
            state_drawn = st;
            draw_pond();
            draw_status();
            next_draw_ms = fm1_ms + 300u;
        } else if ((int32_t)(fm1_ms - next_draw_ms) >= 0) {   /* just the pond's own slow shimmer tick --
                                                                   the title and status text never change here */
            draw_pond();
            draw_isr_time();
            next_draw_ms = fm1_ms + 300u;
        }

        /* settings save: prefer a quiet moment (no audible glitch from the erase, docs/RULES.md), but this is an
           ambient instrument that may rarely go fully silent -- don't wait forever for one */
        if (dirty && (quiet() || fm1_ms - t_change > 30000u) && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
