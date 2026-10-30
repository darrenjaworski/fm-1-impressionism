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
    /* Seam E: colour temperature (ENC_KNOB2, 0..31). Cool dusk at 0, warm gold at 31; red should rise and blue
       should fall as the knob turns up, for both the sky and the water reference colours. */
    {
        rgb_t sky_lo = color_to_sky_rgb(0), sky_hi = color_to_sky_rgb(31);
        rgb_t water_lo = color_to_water_rgb(0), water_hi = color_to_water_rgb(31);
        check(sky_hi.r > sky_lo.r && sky_hi.b < sky_lo.b, "sky warms up (more red, less blue) as color rises");
        check(water_hi.r > water_lo.r && water_hi.b < water_lo.b, "water warms up the same way");
    }

    /* Seam F: the bloom palette. One colour per chord template (CHORD_MAJ7/CHORD_ADD9, planing.h), and they're
       meant to read as distinct at a glance (ROADMAP.md: "lily-pad bloom colour shift" on a chord change). */
    {
        rgb_t maj7 = chord_templ_to_bloom_rgb(0), add9 = chord_templ_to_bloom_rgb(1);
        check(maj7.r != add9.r || maj7.g != add9.g || maj7.b != add9.b, "the two chord templates get visibly different bloom colours");
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

    /* Seam H: ripple phase step (ENC_KNOB1, drift rate 0..3) -> how far the water's ripple phase advances each
       screen tick. Monotonic; 0 still moves (a perfectly frozen pond doesn't read as "slow", it reads as off). */
    {
        uint32_t s0 = ripple_step(0u), s1 = ripple_step(1u), s2 = ripple_step(2u), s3 = ripple_step(3u);
        check(s0 >= 1u, "drift_rate 0 still ripples, just slowly");
        check(s0 < s1 && s1 < s2 && s2 < s3, "ripple speed rises strictly with drift rate");
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

    printf("scene: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
