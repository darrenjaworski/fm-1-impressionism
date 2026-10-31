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

    /* Seam F: the bloom palette. One colour per chord template (CHORD_MAJ7/CHORD_ADD9, planing.h); both anchored
       to the painting's single purple flower colour (user-specified exactly, not a derived hue) --
       the two templates read as distinct via brightness, not by inventing a second colour not in the reference. */
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
       composition, same reasoning as cell_kind below) so the pond's leaves read as varied, not flat. */
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

    /* Seam G: active-cell count (ENC_ALGORITHM, density 0..3) -> how many of the pond's lily pads read as
       "active"/lit, out of some total. Monotonic, never exceeds the total, and density 0 isn't literally zero
       (a pond with nothing ever lit would look dead, not "sparse"). */
    {
        uint32_t total = 40u, c0 = active_cell_count(0u, total), c1 = active_cell_count(1u, total),
                 c2 = active_cell_count(2u, total), c3 = active_cell_count(3u, total);
        check(c0 >= 1u, "density 0 still lights at least one pad");
        check(c0 < c1 && c1 < c2 && c2 < c3, "active count rises strictly with density");
        check(c3 <= total, "never more active cells than exist");
    }

    /* Seam I: the pond's fixed layout. A deterministic hashed pattern generated once, not re-randomised every
       frame (it's the composition, not the reactive part) -- so the same cell is always the same kind, and the
       grid reads as a plausible lily pond: not all water, not all pads, roughly a tenth to a third pads. */
    {
        uint32_t x, y, pads = 0, total = POND_W * POND_H, mismatch = 0;
        for (y = 0; y < POND_H; y++)
            for (x = 0; x < POND_W; x++) {
                if (cell_kind(x, y) != cell_kind(x, y)) mismatch = 1;   /* deterministic: same call, same answer */
                if (cell_kind(x, y) == CELL_PAD) pads++;
            }
        check(!mismatch, "cell_kind is deterministic for the same (x, y)");
        check(pads * 10u >= total && pads * 3u <= total, "pads are a plausible fraction of the pond (10%-33%)");
    }

    /* Seam J: the shared shimmer (water and active blooms both use it). Never brighter than the source colour,
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
