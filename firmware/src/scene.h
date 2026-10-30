/* SPDX-License-Identifier: GPL-3.0-only */
/* The reactive pixel-art lily-pond screen (ROADMAP.md): pure data and pure functions only, no platform
 * dependency (no fm1_*, no screen calls, no floats) -- so tests/scene_test.c exercises this directly with the
 * system compiler, and impressionism.c includes it for the real build (which turns these into RGB565 via its
 * own RGB() macro and the actual lcd_fill/cv_* calls -- that part isn't unit-tested, see docs/RULES.md; it's
 * checked by eye against tests/run_host.sh's screenshots instead). Integers only, per docs/RULES.md. */
#pragma once
#include <stdint.h>

typedef struct { uint8_t r, g, b; } rgb_t;

static rgb_t lerp_rgb(rgb_t a, rgb_t b, uint32_t t, uint32_t t_max)   /* t/t_max of the way from a to b */
{
    rgb_t c;
    c.r = (uint8_t)(a.r + ((int32_t)b.r - a.r) * (int32_t)t / (int32_t)t_max);
    c.g = (uint8_t)(a.g + ((int32_t)b.g - a.g) * (int32_t)t / (int32_t)t_max);
    c.b = (uint8_t)(a.b + ((int32_t)b.b - a.b) * (int32_t)t / (int32_t)t_max);
    return c;
}

/* ENC_KNOB2 (colour), 0..31: the pond's overall temperature, a cool dusk (0) to a warm golden-hour (31) */
#define COLOR_MAX 31u
static rgb_t color_to_sky_rgb(uint32_t color)
{
    rgb_t dusk = { 40, 60, 110 }, gold = { 230, 170, 90 };
    return lerp_rgb(dusk, gold, color, COLOR_MAX);
}
static rgb_t color_to_water_rgb(uint32_t color)
{
    rgb_t dusk = { 20, 50, 70 }, gold = { 120, 130, 50 };
    return lerp_rgb(dusk, gold, color, COLOR_MAX);
}

/* the lily blooms' colour, one per chord template. templ lines up with planing.h's CHORD_MAJ7 (0) / CHORD_ADD9
   (1) by index, not by including planing.h -- scene.h stays decoupled from the music logic, impressionism.c
   wires the two together. */
static const rgb_t CHORD_BLOOM_RGB[2] = {
    { 255, 182, 213 },   /* maj7: a soft pink */
    { 221, 190, 255 },   /* add9: a pale violet */
};
static rgb_t chord_templ_to_bloom_rgb(uint32_t templ) { return CHORD_BLOOM_RGB[templ]; }

/* ENC_ALGORITHM (density), 0..3 -> how many of the pond's "pad" cells read as active/lit, out of some total.
   Roughly an eighth at density 0 (never zero: a totally dark pond reads as dead, not sparse), up to roughly
   two-thirds at density 3. Tune by eye once this is on the real screen. */
static uint32_t active_cell_count(uint32_t density, uint32_t total)
{
    static const uint32_t EIGHTHS[4] = { 1u, 2u, 4u, 6u };   /* 1/8, 1/4, 1/2, 3/4 of total -- clear separation
                                                                 at any realistic total, not just a tested one */
    uint32_t n = total * EIGHTHS[density] / 8u;
    return n < 1u ? 1u : n;
}

/* ENC_KNOB1 (drift rate), 0..3 -> the water's ripple phase step per screen tick (an arbitrary small unit; what
   matters is the ordering and that it's never 0). Tune the actual range by eye on real hardware. */
static uint32_t ripple_step(uint32_t drift_rate)
{
    static const uint32_t STEP[4] = { 1u, 2u, 4u, 7u };
    return STEP[drift_rate];
}

/* ---- the pond's fixed layout: a deterministic hashed pattern, generated once (the composition), not
   re-randomised per frame (the reactive parts above are what change) ------------------------------------------ */
#define POND_W 24u   /* 240 px / 10 px cells */
#define POND_H 12u   /* 120 px tall: one cv_* canvas strip, one DMA blit. platform.c's own CV_MAX default is only
                        240x64 (not gfx.c's unused 240x124 fallback -- platform.c defines it first); this needs
                        impressionism.c to raise CV_MAX to 240x120 before #include "platform.c" (it does). The
                        rest of the 240 px screen is the title and the status-text overlay (impressionism.c). */
enum { CELL_WATER = 0, CELL_PAD = 1 };

static uint32_t cell_hash(uint32_t x, uint32_t y)   /* a cheap 2D hash: deterministic, no table, no state */
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static uint32_t cell_kind(uint32_t x, uint32_t y)
{
    return (cell_hash(x, y) % 100u) < 22u ? CELL_PAD : CELL_WATER;   /* ~22% pads: tune by eye on real hardware */
}
