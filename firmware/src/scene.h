/* SPDX-License-Identifier: GPL-3.0-only */
/* The lily-pond screen (ROADMAP.md "Screen": Monet's Water Lilies; stationary lilies, continuous flowing water,
 * motion never tied to the panel): pure data and pure functions only, no platform
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

/* Monet's Water Lilies palette (user-specified, 2026-10-30, picked by eye from the reference painting): purple
   flowers over blue-green water and teal-green pads. Every reactive colour below is anchored to one of these
   five, not an invented hue -- "support this colour palette" means the whole pond stays inside it. */

/* Water (ENC_KNOB2 controls brightness: dark at 0, light at 31) */
#define MONET_WATER_DARK_R 0x3Bu
#define MONET_WATER_DARK_G 0x66u
#define MONET_WATER_DARK_B 0x8Cu
#define MONET_WATER_LIGHT_R 0x58u
#define MONET_WATER_LIGHT_G 0x88u
#define MONET_WATER_LIGHT_B 0xA6u

/* Lily pad leaves (two tones per cell for texture) */
#define MONET_LEAF_DARK_R 0x3Cu
#define MONET_LEAF_DARK_G 0x7Au
#define MONET_LEAF_DARK_B 0x6Au
#define MONET_LEAF_LIGHT_R 0x8Du
#define MONET_LEAF_LIGHT_G 0xB8u
#define MONET_LEAF_LIGHT_B 0x5Fu

/* Flower blooms: the one purple, dimmed per chord template (see chord_templ_to_bloom_rgb) */
#define MONET_FLOWER_R 0x84u
#define MONET_FLOWER_G 0x51u
#define MONET_FLOWER_B 0xA6u

/* ENC_KNOB2 (colour), 0..31: the water's own tone, the painting's deep blue (0) to its lit blue-teal (31) --
   brightness only, no hue shift, since the reference palette has no separate warm tone to shift toward. */
#define COLOR_MAX 31u
static rgb_t color_to_water_rgb(uint32_t color)
{
    rgb_t dark = { MONET_WATER_DARK_R, MONET_WATER_DARK_G, MONET_WATER_DARK_B };
    rgb_t light = { MONET_WATER_LIGHT_R, MONET_WATER_LIGHT_G, MONET_WATER_LIGHT_B };
    return lerp_rgb(dark, light, color, COLOR_MAX);
}

/* the lily blooms' colour, one per chord template. The table is indexed by planing.h's CHORD_* values by position,
   not by including planing.h -- scene.h stays decoupled from the music logic, impressionism.c wires the two
   together. Every template is the painting's one flower purple (#8451A6, user-specified exactly for maj7), at its
   own percent of that brightness: no second hue, only the dimming tells the chords apart. */
static rgb_t chord_templ_to_bloom_rgb(uint32_t templ)
{
    static const uint32_t BLOOM_PCT[8] = { 100u, 70u, 88u, 76u, 94u, 82u, 91u, 73u };
    rgb_t c = { MONET_FLOWER_R, MONET_FLOWER_G, MONET_FLOWER_B };
    uint32_t k = BLOOM_PCT[templ];
    c.r = (uint8_t)((uint32_t)c.r * k / 100u);
    c.g = (uint8_t)((uint32_t)c.g * k / 100u);
    c.b = (uint8_t)((uint32_t)c.b * k / 100u);
    return c;
}

/* ENC_ALGORITHM (density), 0..3 -> how many of the pond's "pad" cells read as active/lit (purple blooms), out of
   some total. Sparsest density blooms the most pads, so the densest setting leaves more green lilies showing:
   about three-quarters at density 0, down to an eighth at density 3 (never zero: a totally dark pond reads as
   dead, not sparse). Tune by eye once this is on the real screen. */
static uint32_t active_cell_count(uint32_t density, uint32_t total)
{
    static const uint32_t EIGHTHS[4] = { 6u, 4u, 2u, 1u };   /* 3/4, 1/2, 1/4, 1/8 of total */
    uint32_t n = total * EIGHTHS[density] / 8u;
    return n < 1u ? 1u : n;
}

/* a travelling shimmer on a colour: shimmer 0..7 scales it from 80/115 to 115/115 of its brightness (water and
   active blooms both ripple with this, so the purple moves with the pond) */
static rgb_t shimmer_rgb(rgb_t c, uint32_t shimmer)
{
    uint32_t k = 80u + shimmer * 5u;
    c.r = (uint8_t)((uint32_t)c.r * k / 115u);
    c.g = (uint8_t)((uint32_t)c.g * k / 115u);
    c.b = (uint8_t)((uint32_t)c.b * k / 115u);
    return c;
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

/* a PAD cell's leaf colour: one of the painting's two greens, picked per cell (fixed, like cell_kind -- the
   pond's composition, not something that reacts) so the leaves read as varied rather than flat */
static rgb_t pad_leaf_rgb(uint32_t x, uint32_t y)
{
    rgb_t dark = { MONET_LEAF_DARK_R, MONET_LEAF_DARK_G, MONET_LEAF_DARK_B };
    rgb_t light = { MONET_LEAF_LIGHT_R, MONET_LEAF_LIGHT_G, MONET_LEAF_LIGHT_B };
    return (cell_hash(x, y) >> 3 & 1u) ? light : dark;   /* a different bit than cell_kind's %100 uses */
}
