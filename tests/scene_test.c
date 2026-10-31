/* SPDX-License-Identifier: GPL-3.0-only */
/* The reactive pixel-art lily-pond screen's pure logic (firmware/src/scene.h), ROADMAP.md's "What the screen
 * actually shows". No platform dependency, so this runs with the plain system compiler:
 * $CC -o build/host/scene_test tests/scene_test.c && build/host/scene_test */
#include <stdio.h>
#include "../firmware/src/scene.h"
static int fails;
static void check(int ok, const char *what) { printf("scene: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    /* Seam E: colour temperature (ENC_KNOB2, 0..31) -> the water's own tone, darker at 0, lighter at 31. The
       exact endpoints are Monet's Water Lilies palette, picked by eye from the reference painting (user,
       2026-10-30), not placeholders -- locked in literally, not just "brighter", so a future edit can't drift
       off the actual reference colours without this test noticing. */
    {
        rgb_t water_lo = color_to_water_rgb(0), water_hi = color_to_water_rgb(31);
        check(water_lo.r == MONET_WATER_DARK_R && water_lo.g == MONET_WATER_DARK_G && water_lo.b == MONET_WATER_DARK_B, "water at color=0 is the painting's deep blue");
        check(water_hi.r == MONET_WATER_LIGHT_R && water_hi.g == MONET_WATER_LIGHT_G && water_hi.b == MONET_WATER_LIGHT_B, "water at color=31 is the painting's lit blue-teal");
        check(water_hi.r > water_lo.r && water_hi.g > water_lo.g && water_hi.b > water_lo.b, "water brightens uniformly as color rises (no hue shift, just darker/lighter)");
    }

    /* Seam F: the bloom palette. One colour per chord template: template 0 (CHORD_MAJ7 in planing.h) is the
       painting's single purple flower colour (user-specified exactly, not a derived hue); template 1 (CHORD_ADD9)
       is a dimmer shade of that same purple. Distinct via brightness, not by inventing a second colour. */
    {
        rgb_t maj7 = chord_templ_to_bloom_rgb(0), add9 = chord_templ_to_bloom_rgb(1);
        check(maj7.r == MONET_FLOWER_R && maj7.g == MONET_FLOWER_G && maj7.b == MONET_FLOWER_B, "maj7's bloom is the painting's flower purple");
        check(maj7.r != add9.r || maj7.g != add9.g || maj7.b != add9.b, "the two chord templates get visibly different bloom brightness");
        check(add9.r <= maj7.r && add9.g <= maj7.g && add9.b <= maj7.b, "add9 is a dimmer shade of the same purple, not a different hue");
        check(add9.r == 92 && add9.g == 56 && add9.b == 116, "add9's bloom is 70% of the flower purple: (92,56,116)");
    }

    /* Seam F3: each of the 8 chord templates has its own bloom shade (scene.h's per-template brightness table).
       All distinct, none brighter than maj7, none black. */
    {
        uint32_t a, b, dup = 0, brighter = 0, black = 0;
        rgb_t maj7 = chord_templ_to_bloom_rgb(0);
        for (a = 0; a < 8u; a++) {
            rgb_t ca = chord_templ_to_bloom_rgb(a);
            if (ca.r == 0 && ca.g == 0 && ca.b == 0) black = 1;
            if (ca.r > maj7.r || ca.g > maj7.g || ca.b > maj7.b) brighter = 1;
            for (b = a + 1; b < 8u; b++) {
                rgb_t cb = chord_templ_to_bloom_rgb(b);
                if (ca.r == cb.r && ca.g == cb.g && ca.b == cb.b) dup = 1;
            }
        }
        check(!dup, "all 8 chord templates have distinct bloom colours");
        check(!brighter, "no chord template's bloom is brighter than maj7's");
        check(!black, "no chord template's bloom is black");
    }

    /* Seam F2: the pad/leaf colours -- also from the reference painting, picked per cell (deterministic, fixed
       composition, same reasoning as the pond layout below) so the pond's leaves read as varied, not flat. */
    {
        uint32_t x, y, saw_dark = 0, saw_light = 0, other = 0;
        for (y = 0; y < POND_H; y++)
            for (x = 0; x < POND_W; x++) {
                rgb_t c = pad_leaf_rgb(x, y);
                if (c.r == MONET_LEAF_DARK_R && c.g == MONET_LEAF_DARK_G && c.b == MONET_LEAF_DARK_B) saw_dark = 1;
                else if (c.r == MONET_LEAF_LIGHT_R && c.g == MONET_LEAF_LIGHT_G && c.b == MONET_LEAF_LIGHT_B) saw_light = 1;
                else other = 1;
            }
        check(saw_dark && saw_light, "both leaf tones turn up across the pond");
        check(!other, "leaf colour is always one of exactly those two tones, nothing else");
    }

    /* Seam I: the pond's fixed layout. A deterministic hashed pattern generated once, not re-randomised every
       frame (it's the composition, not the reactive part) -- so the grid reads as a plausible lily pond: not all
       water, not all pads, roughly a tenth to a third pads. The pad count is measured from the fixed layout
       (POND_W x POND_H = 288 cells, 60 of them pads); a change to cell_kind's hash moves it, on purpose. */
    {
        uint32_t x, y, pads = 0, total = POND_W * POND_H;
        for (y = 0; y < POND_H; y++)
            for (x = 0; x < POND_W; x++)
                if (cell_kind(x, y) == CELL_PAD) pads++;
        check(total == 288u, "the pond is 24 x 12 = 288 cells");
        check(pads == 60u, "the fixed layout has exactly 60 pad cells (measured)");
        check(pads * 10u >= total && pads * 3u <= total, "pads are a plausible fraction of the pond (10%-33%)");
    }

    /* Seam G: which pads bloom (purple) at each density, 0..3. The bloom test is fixed per cell, so the counts
       over the 60 pads are fixed too (measured from the layout: 46, 30, 18, 8). Density 0 blooms the most pads
       and density 3 the fewest, but never none. */
    {
        uint32_t x, y, d, n[4] = { 0, 0, 0, 0 }, top[4] = { 0, 0, 0, 0 }, bot[4] = { 0, 0, 0, 0 }, nested = 0;
        for (y = 0; y < POND_H; y++)
            for (x = 0; x < POND_W; x++) {
                if (cell_kind(x, y) != CELL_PAD) continue;
                for (d = 0; d < 4u; d++) {
                    if (!pad_blooms(x, y, d)) continue;
                    n[d]++;
                    if (y < POND_H / 2u) top[d]++; else bot[d]++;
                }
                for (d = 0; d < 3u; d++)   /* nested: a pad blooming at a denser setting blooms at every sparser one */
                    if (pad_blooms(x, y, d + 1u) && !pad_blooms(x, y, d)) nested = 1;
            }
        check(n[0] == 46u && n[1] == 30u && n[2] == 18u && n[3] == 8u, "blooming pads per density, of 60 pads: 46, 30, 18, 8");
        check(n[0] > n[1] && n[1] > n[2] && n[2] > n[3], "bloom count falls strictly with density: the densest setting shows the most green lilies");
        check(n[3] >= 1u, "density 3 still blooms at least one pad");
        check(!nested, "a pad that blooms at a denser setting also blooms at every sparser one");
        check(top[0] > 0u && top[1] > 0u && top[2] > 0u && top[3] > 0u, "every density blooms pads in the top half of the pond");
        check(bot[0] > 0u && bot[1] > 0u && bot[2] > 0u && bot[3] > 0u, "every density blooms pads in the bottom half of the pond");
    }

    /* Seam K: the pond's pixel geometry. A cell is CELL_PX square, so the 24 x 12 cells are the 240 x 120 pond. */
    {
        check(CELL_PX == 10u, "a cell is 10 px square");
        check(POND_W * CELL_PX == 240u && POND_H * CELL_PX == 120u, "the cells make a 240 x 120 px pond");
    }

    /* Seam L: the triangle wave under the shimmer. Period 16, 0 up to 7 and back down to 0. */
    {
        check(tri8(0u) == 0u && tri8(7u) == 7u && tri8(8u) == 7u && tri8(15u) == 0u, "tri8: 0 at 0, 7 at 7 and 8, 0 at 15");
        check(tri8(17u) == 1u && tri8(16u) == 0u, "tri8 repeats every 16");
    }

    /* Seam M: the water. Its shimmer stays 0..7, neighbouring cells differ by at most 2 (the waves are smooth, no
       hard seams between cells), and the water actually moves as the phase advances. */
    {
        uint32_t x, y, phase, out_of_range = 0, too_steep = 0, moved = 0;
        for (phase = 0; phase < 500u; phase++)
            for (y = 0; y < POND_H; y++)
                for (x = 0; x < POND_W; x++) {
                    uint32_t s = water_shimmer(x, y, phase);
                    if (s > 7u) out_of_range = 1;
                    if (x + 1u < POND_W && (int32_t)water_shimmer(x + 1u, y, phase) - (int32_t)s > 2) too_steep = 1;
                    if (x + 1u < POND_W && (int32_t)s - (int32_t)water_shimmer(x + 1u, y, phase) > 2) too_steep = 1;
                    if (y + 1u < POND_H && (int32_t)water_shimmer(x, y + 1u, phase) - (int32_t)s > 2) too_steep = 1;
                    if (y + 1u < POND_H && (int32_t)s - (int32_t)water_shimmer(x, y + 1u, phase) > 2) too_steep = 1;
                    if (phase > 0u && s != water_shimmer(x, y, 0u)) moved = 1;
                }
        check(!out_of_range, "water shimmer is always within 0..7");
        check(!too_steep, "neighbouring water cells differ by at most 2 shimmer steps, at every phase");
        check(moved, "the water moves: some cell's shimmer changes as the phase advances");
    }

    /* Seam N: the pads' wander. A pad sits within 2 px of its cell's corner, and the whole 10 px pad stays inside
       the 240 x 120 pond, at every phase. The wander changes with the phase (it is not frozen at one spot). */
    {
        uint32_t x, y, phase, axis, off_by_too_much = 0, outside = 0, moved = 0;
        for (phase = 0; phase < 3000u; phase++)
            for (y = 0; y < POND_H; y++)
                for (x = 0; x < POND_W; x++)
                    for (axis = 0; axis < 2u; axis++) {
                        int32_t p = pad_pos(x, y, phase, axis), base = (int32_t)((axis ? y : x) * CELL_PX);
                        int32_t hi = (int32_t)((axis ? POND_H : POND_W) * CELL_PX) - (int32_t)CELL_PX;
                        if (p - base > 2 || base - p > 2) off_by_too_much = 1;
                        if (p < 0 || p > hi) outside = 1;
                        if (phase > 0u && p != pad_pos(x, y, 0u, axis)) moved = 1;
                    }
        check(!off_by_too_much, "a pad wanders at most 2 px from its cell's corner, at every phase");
        check(!outside, "the whole 10 px pad stays inside the 240 x 120 pond, at every phase");
        check(moved, "pads wander: some pad's position changes as the phase advances");
    }

    /* Seam J: the shared shimmer (water and blooms both use it). Never brighter than the source colour,
       never black, and brightest at the top of its 0..7 range. */
    {
        rgb_t bloom = chord_templ_to_bloom_rgb(0), lo = shimmer_rgb(bloom, 0), hi = shimmer_rgb(bloom, 7);
        check(hi.r == bloom.r && hi.g == bloom.g && hi.b == bloom.b, "shimmer 7 leaves the colour at full brightness");
        check(lo.r < hi.r && lo.g < hi.g && lo.b < hi.b, "shimmer 0 is dimmer than shimmer 7 on every channel");
        check(lo.r > 0 && lo.g > 0 && lo.b > 0, "the lowest shimmer never goes black");
    }

    printf("scene: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
