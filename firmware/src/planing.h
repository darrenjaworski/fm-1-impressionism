/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine (ROADMAP.md v1): scales, chord templates, the planing picker and the voice trigger
 * scheduler. Pure data and pure functions only, no platform dependency (no fm1_*, no flash, no floats) — so
 * tests/planing_test.c exercises this directly with the system compiler, and impressionism.c includes it for
 * the real build. Integers only, per docs/RULES.md. */
#pragma once
#include <stdint.h>

/* ---- scales (CLAUDE.md Mission; ROADMAP.md v1 narrows to these two) ---------------------------------------- */

enum { SCALE_WHOLETONE = 0, SCALE_PENTATONIC = 1, SCALE_COUNT = 2 };

static const int8_t SCALE_WHOLETONE_ST[6] = { 0, 2, 4, 6, 8, 10 };   /* six whole tones */
static const int8_t SCALE_PENTATONIC_ST[5] = { 0, 2, 4, 7, 9 };      /* major pentatonic */

static const struct { const int8_t *st; uint32_t n; } SCALE_TABLE[SCALE_COUNT] = {
    { SCALE_WHOLETONE_ST, 6u },
    { SCALE_PENTATONIC_ST, 5u },
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

/* ---- chord templates (ROADMAP.md v1): the drone is the root; these are the 3 colour voices above it ------- */

enum { CHORD_MAJ7 = 0, CHORD_ADD9 = 1, CHORD_TEMPLATE_COUNT = 2 };

static const int8_t CHORD_MAJ7_ST[3] = { 4, 7, 11 };
static const int8_t CHORD_ADD9_ST[3] = { 4, 7, 14 };
static const int8_t *const CHORD_TABLE[CHORD_TEMPLATE_COUNT] = { CHORD_MAJ7_ST, CHORD_ADD9_ST };

/* voice: 0..2 -> semitone offset above the root (fixed per template, independent of the current scale) */
static int32_t chord_voice_semitone(uint32_t templ, uint32_t voice)
{
    return CHORD_TABLE[templ][voice];
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

typedef struct {
    int32_t degree;    /* the new root's scale degree (relative to wherever the drone already is) */
    uint32_t templ;    /* CHORD_MAJ7 or CHORD_ADD9 */
} plan_t;

/* drift_rate: 0..3 (ENC_KNOB1, ROADMAP.md), bigger = a wider step each time. Design choice, not spelled out in
   ROADMAP.md: drift_rate sizes the step here; "density" (how OFTEN this gets called) is Seam D's job, not this
   function's -- it only decides HOW FAR, given that it is being called at all. */
static plan_t plan_next(uint32_t scale, int32_t prev_degree, uint32_t drift_rate, uint32_t *rng)
{
    int32_t span = 1 + (int32_t)drift_rate;             /* steps of -span..+span */
    int32_t step = (int32_t)(rng_next(rng) % (uint32_t)(2 * span + 1)) - span;
    plan_t p;
    (void)scale;                                         /* the degree is scale-relative; scale_degree_to_semitone
                                                              resolves it later. Kept as a parameter for symmetry
                                                              and in case a future scale needs a different walk. */
    p.degree = prev_degree + step;
    p.templ = rng_next(rng) % CHORD_TEMPLATE_COUNT;
    return p;
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
