/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine (ROADMAP.md): scales, chord templates, the planing picker and the voice trigger
 * scheduler. Pure data and pure functions only, no platform dependency (no fm1_*, no flash, no floats) — so
 * tests/planing_test.c exercises this directly with the system compiler, and impressionism.c includes it for
 * the real build. Integers only, per docs/RULES.md. */
#pragma once
#include <stdint.h>

/* ---- scales (CLAUDE.md Mission, ROADMAP.md): ENC_SELECT cycles through these ----------------------------------- */

enum {
    SCALE_WHOLETONE = 0, SCALE_PENTATONIC = 1, SCALE_MINOR_PENTATONIC = 2,
    SCALE_LYDIAN = 3, SCALE_MIXOLYDIAN = 4, SCALE_DORIAN = 5, SCALE_COUNT = 6
};

static const int8_t SCALE_WHOLETONE_ST[6] = { 0, 2, 4, 6, 8, 10 };   /* six whole tones */
static const int8_t SCALE_PENTATONIC_ST[5] = { 0, 2, 4, 7, 9 };      /* major pentatonic */
static const int8_t SCALE_MINOR_PENTATONIC_ST[5] = { 0, 3, 5, 7, 10 };
static const int8_t SCALE_LYDIAN_ST[7] = { 0, 2, 4, 6, 7, 9, 11 };
static const int8_t SCALE_MIXOLYDIAN_ST[7] = { 0, 2, 4, 5, 7, 9, 10 };
static const int8_t SCALE_DORIAN_ST[7] = { 0, 2, 3, 5, 7, 9, 10 };

static const struct { const int8_t *st; uint32_t n; } SCALE_TABLE[SCALE_COUNT] = {
    { SCALE_WHOLETONE_ST, 6u },
    { SCALE_PENTATONIC_ST, 5u },
    { SCALE_MINOR_PENTATONIC_ST, 5u },
    { SCALE_LYDIAN_ST, 7u },
    { SCALE_MIXOLYDIAN_ST, 7u },
    { SCALE_DORIAN_ST, 7u },
};

/* degree: any integer, wraps through the scale and the octaves either way. -> semitone offset from the
   scale's own root (negative below it, >= 12 an octave or more above it). */
static int32_t scale_degree_to_semitone(uint32_t scale, int32_t degree)
{
    int32_t n = (int32_t)SCALE_TABLE[scale].n, oct = 0, d = degree;
    while (d < 0) { d += n; oct--; }
    while (d >= n) { d -= n; oct++; }
    return oct * 12 + SCALE_TABLE[scale].st[d];
}

/* the register limit: the nearest degree to `degree` on the way to 0 whose semitone (scale_degree_to_semitone) lies in
   lo_st..hi_st. Steps one degree at a time toward 0, so a degree already in range is returned unchanged. The bounds
   must include 0, which makes this stop at 0 at the latest. */
static int32_t clamp_register(uint32_t scale, int32_t degree, int32_t lo_st, int32_t hi_st)
{
    int32_t st;
    for (;;) {
        st = scale_degree_to_semitone(scale, degree);
        if (degree == 0 || (st >= lo_st && st <= hi_st)) return degree;
        degree += degree > 0 ? -1 : 1;
    }
}

/* e detents from this scale (either way, any size), wrapping through all SCALE_COUNT scales */
static uint32_t scale_step(uint32_t scale, int32_t e)
{
    int32_t s = ((int32_t)scale + e) % (int32_t)SCALE_COUNT;
    return (uint32_t)(s < 0 ? s + (int32_t)SCALE_COUNT : s);
}

/* ---- chord templates (ROADMAP.md): the drone is the root; these are the 3 colour voices above it ------------- */

enum {
    CHORD_MAJ7 = 0, CHORD_ADD9 = 1, CHORD_SUS2 = 2, CHORD_SUS4 = 3,
    CHORD_SIX9 = 4, CHORD_QUARTAL = 5, CHORD_9NO5 = 6, CHORD_AUG9 = 7, CHORD_TEMPLATE_COUNT = 8
};

static const int8_t CHORD_MAJ7_ST[3] = { 4, 7, 11 };
static const int8_t CHORD_ADD9_ST[3] = { 4, 7, 14 };
static const int8_t CHORD_SUS2_ST[3] = { 2, 7, 12 };
static const int8_t CHORD_SUS4_ST[3] = { 5, 7, 12 };
static const int8_t CHORD_SIX9_ST[3] = { 4, 9, 14 };
static const int8_t CHORD_QUARTAL_ST[3] = { 5, 10, 15 };
static const int8_t CHORD_9NO5_ST[3] = { 4, 10, 14 };
static const int8_t CHORD_AUG9_ST[3] = { 4, 8, 14 };
static const int8_t *const CHORD_TABLE[CHORD_TEMPLATE_COUNT] = {
    CHORD_MAJ7_ST, CHORD_ADD9_ST, CHORD_SUS2_ST, CHORD_SUS4_ST, CHORD_SIX9_ST, CHORD_QUARTAL_ST, CHORD_9NO5_ST,
    CHORD_AUG9_ST,
};

/* voice: 0..2 -> semitone offset above the root (fixed per template, independent of the current scale) */
static int32_t chord_voice_semitone(uint32_t templ, uint32_t voice)
{
    return CHORD_TABLE[templ][voice];
}

/* pitch class (0..11) of a semitone offset, for any sign */
static int32_t pitch_class(int32_t semitone)
{
    int32_t pc = semitone % 12;
    return pc < 0 ? pc + 12 : pc;
}

/* true when each of the template's 3 voices, above this degree's root, lands on a note of the scale (compared
   by pitch class, so an octave away still counts). Depends on the degree as well as the scale. */
static int chord_fits(uint32_t scale, int32_t degree, uint32_t templ)
{
    int32_t root = scale_degree_to_semitone(scale, degree), v;
    for (v = 0; v < 3; v++) {
        int32_t pc = pitch_class(root + chord_voice_semitone(templ, (uint32_t)v));
        uint32_t i, in = 0;
        for (i = 0; i < SCALE_TABLE[scale].n; i++)
            if (pitch_class(SCALE_TABLE[scale].st[i]) == pc) in = 1;
        if (!in) return 0;
    }
    return 1;
}

/* ---- the planing picker: a random walk across scale degrees, replanned on each retrigger ------------------- */

/* xorshift32 (Marsaglia): fast, deterministic given its seed, integers only. Never seed with 0. */
static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

/* a uniform pick among the templates that fit this degree. Every degree has at least one (planing_test.c checks
   it), so the modulus is never zero. */
static uint32_t pick_chord(uint32_t scale, int32_t degree, uint32_t *rng)
{
    uint32_t t, n = 0, pick;
    for (t = 0; t < CHORD_TEMPLATE_COUNT; t++)
        if (chord_fits(scale, degree, t)) n++;
    pick = rng_next(rng) % n;
    for (t = 0; t < CHORD_TEMPLATE_COUNT; t++)
        if (chord_fits(scale, degree, t) && pick-- == 0u) return t;
    return 0u;
}

/* drift_rate: 0..3 (ENC_KNOB1, ROADMAP.md), bigger = a wider step each time. Design choice, not spelled out in
   ROADMAP.md: drift_rate sizes the step here; "density" (how OFTEN this gets called) is Seam D's job, not this
   function's -- it only decides HOW FAR, given that it is being called at all. Returns the new degree. */
static int32_t plan_next(int32_t prev_degree, uint32_t drift_rate, uint32_t *rng)
{
    int32_t span = 1 + (int32_t)drift_rate;             /* steps of -span..+span */
    int32_t step = (int32_t)(rng_next(rng) % (uint32_t)(2 * span + 1)) - span;
    return prev_degree + step;
}

/* ---- the voice trigger scheduler: how long until the next retrigger -----------------------------------------
 * density: 0..3 (ENC_ALGORITHM, ROADMAP.md): "how often voices retrigger". Ranges picked by ear-judgement, not
 * from any spec -- wide enough that each level is clearly slower/faster than its neighbours, narrow enough that
 * a single level still feels alive rather than metronomic. In milliseconds (fm1_ms), not raw fm1_ticks: a drone
 * can go minutes between retriggers, well past where 24 MHz ticks wrap (~179 s). */
static const struct { uint32_t min_ms, max_ms; } DENSITY_TABLE[4] = {
    { 8000u, 20000u },
    { 5000u, 12000u },
    { 3000u, 7000u },
    { 1200u, 3500u },
};

static uint32_t next_trigger_ticks(uint32_t density, uint32_t *rng)
{
    uint32_t lo = DENSITY_TABLE[density].min_ms, hi = DENSITY_TABLE[density].max_ms;
    return lo + rng_next(rng) % (hi - lo + 1u);
}
