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
 *   MASTER         volume (platform.c; nothing to do here)
 * scale/density/drift/colour/drone-on persist across power-off; the current wander position does not (it
 * re-randomizes fresh each boot -- only the knob settings are meant to survive, ROADMAP.md).
 *
 * The screen for now is plain status text (current scale, the four settings) -- ROADMAP.md's reactive
 * impressionist pixel-art lily pond is a separate, later slice, not yet built.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "IMPRESSIONISM"                /* the rescue and the crash screen say this (docs/MAKE-IT-YOURS.md) */
static void app_idle(void) {}                   /* nothing to do while the screen's DMA runs: no MIDI in, in v1 */
#define LCD_IDLE app_idle
#include "platform.c"
#include "planing.h"                            /* scales, chord templates, the planing picker, the scheduler */

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define NV 4u                                   /* 0 = drone, 1..3 = the chord's colour tones */
static struct {
    volatile uint32_t inc;                      /* phase step per frame: f * 2^32 / 44100 */
    volatile int32_t gate;                      /* target level, Q15-ish: 0 = released */
    int32_t amp;                                /* the level now: glides to gate, no clicks */
    uint32_t ph;
} voice[NV];
#define GATE_FULL 8000                          /* 4 voices at this level sum to 32000, under full scale */

/* attack/release glide once per audio block (docs/RULES.md: envelopes need not run every sample), ~5.8 ms a
   block at 256 frames/44.1 kHz -- sized for a few seconds each (soft-focus, CLAUDE.md's Mission), tune by ear
   once this is heard on real hardware */
#define ATTACK_STEP 19                          /* ~2.4 s, 0 -> GATE_FULL */
#define RELEASE_STEP 12                         /* ~3.9 s, GATE_FULL -> 0 */

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
    for (v = 0; v < NV; v++) {
        uint32_t ph = voice[v].ph, step = voice[v].inc;
        int32_t amp = voice[v].amp, g = voice[v].gate;
        if (amp < g) { amp += ATTACK_STEP; if (amp > g) amp = g; }        /* once per block, not per sample */
        else if (amp > g) { amp -= RELEASE_STEP; if (amp < g) amp = g; }
        voice[v].amp = amp;
        if (!amp) continue;                     /* silent: costs nothing (g == 0 too, once amp has caught up) */
        for (i = 0; i < n; i++) {
            int32_t s = (sine(ph) * amp) >> 15;
            ph += step;
            out[2u * i] += s;                   /* L */
            out[2u * i + 1u] += s;              /* R */
        }
        voice[v].ph = ph;
    }
    /* the "colour" knob: a one-pole low-pass on the finished mix, docs/MAKE-IT-YOURS.md's own suggested formula */
    for (i = 0; i < n; i++) {
        lp_l += (out[2u * i] - lp_l) * filter_k >> 15;
        lp_r += (out[2u * i + 1u] - lp_r) * filter_k >> 15;
        out[2u * i] = lp_l;
        out[2u * i + 1u] = lp_r;
    }
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

static uint32_t rng_state = 0x9E3779B9u;         /* never 0 (planing.h's xorshift32) */
static int32_t home_degree, plane_degree;        /* the drone's root, and the chord's offset above it */
static uint32_t chord_templ;
static uint32_t next_plane_ms, next_root_ms;

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
    plan_t p = plan_next(scale, plane_degree, drift, &rng_state);
    plane_degree = clamp_degree(p.degree);
    chord_templ = p.templ;
    retune_voices(scale);
    next_plane_ms = fm1_ms + next_trigger_ticks(density, &rng_state);
}

/* voice 0: re-roots on its own slower, independent timer (not density -- ROADMAP.md says density is "how often
   the 3 voices retrigger", the drone is deliberately not on that clock), using the same drift_rate for its step
   size and resetting the chord back onto the new root when it moves (flagged design choice, see ROADMAP.md) */
static void reroot(uint32_t scale, uint32_t drift)
{
    plan_t p = plan_next(scale, home_degree, drift, &rng_state);
    home_degree = clamp_degree(p.degree);
    plane_degree = 0;
    retune_voices(scale);
    next_root_ms = fm1_ms + 60000u + rng_next(&rng_state) % 120001u;   /* 60-180 s: much slower than any plane rate */
}

/* ---- settings (what survives a power-off, ROADMAP.md) */
static struct { uint8_t scale, density, drift, color, drone_on; } set = { SCALE_PENTATONIC, 1, 1, 16, 1 };

static int32_t color_to_filter_k(uint32_t color) { return 1024 + (int32_t)color * 1024; }   /* 0..31 -> 1024..32768 */

static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].amp) return 0;
    return 1;
}

/* ---- the screen: plain status text for now (ROADMAP.md's pixel-art lily pond is a later slice) */
#define C_ACC RGB(180, 140, 255)
#define C_OFF RGB(90, 90, 90)
static const char *const SCALE_NAME[SCALE_COUNT] = {"WHOLE-TONE", "PENTATONIC"};

static void draw_screen(void)
{
    char s[32];
    uint32_t i = 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 52, 240, &FONT_S, "v" APP_VER "  (pixel art: not yet built)", C_OFF, 1);
    draw_text_box(0, 100, 240, &FONT_L, SCALE_NAME[set.scale], C_WHITE, 1);
    {
        const char *p = set.drone_on ? "DRONE ON " : "DRONE OFF";
        while (*p) s[i++] = *p++;
        s[i++] = ' '; s[i++] = ' ';
        s[i++] = 'D'; s[i++] = (char)('0' + set.density);   /* density */
        s[i++] = ' '; s[i++] = ' ';
        s[i++] = 'R'; s[i++] = (char)('0' + set.drift);     /* drift rate */
        s[i] = 0;
    }
    draw_text_box(0, 150, 240, &FONT_S, s, C_OFF, 1);
}

static void app_main(void)
{
    uint32_t t_change = 0, dirty = 0, scale_drawn = ~0u, state_drawn = ~0u, b, rel, st;
    int32_t e;

    if (settings_load(&set, sizeof set) || set.scale >= SCALE_COUNT || set.density > 3u || set.drift > 3u ||
        set.color > 31u || set.drone_on > 1u)
        set.scale = SCALE_PENTATONIC, set.density = 1, set.drift = 1, set.color = 16, set.drone_on = 1;
    filter_k = color_to_filter_k(set.color);
    reroot(set.scale, set.drift);                /* start with a real root and chord already in place */
    replane(set.scale, set.density, set.drift);
    draw_screen();
    scale_drawn = set.scale;
    state_drawn = set.density | set.drift << 2 | set.drone_on << 4;

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

        if (scale_drawn != set.scale) {          /* the scale changed: re-root and re-plane right away, since a
                                                     degree means a different interval in a different scale */
            home_degree = 0; plane_degree = 0;
            reroot(set.scale, set.drift);
            replane(set.scale, set.density, set.drift);
            scale_drawn = set.scale;
        }
        if ((int32_t)(fm1_ms - next_root_ms) >= 0) reroot(set.scale, set.drift);
        if ((int32_t)(fm1_ms - next_plane_ms) >= 0) replane(set.scale, set.density, set.drift);
        voice[0].gate = set.drone_on ? GATE_FULL : 0;
        {
            uint32_t v;
            for (v = 1; v < NV; v++) voice[v].gate = GATE_FULL;
        }

        if ((st = set.density | set.drift << 2 | set.drone_on << 4) != state_drawn) { state_drawn = st; draw_screen(); }

        /* settings save: prefer a quiet moment (no audible glitch from the erase, docs/RULES.md), but this is an
           ambient instrument that may rarely go fully silent -- don't wait forever for one */
        if (dirty && (quiet() || fm1_ms - t_change > 30000u) && fm1_ms - t_change > 2000u) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
