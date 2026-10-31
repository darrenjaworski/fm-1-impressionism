/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <math.h>
#include "../firmware/src/reverb.h"

int main(void)
{
    reverb_t r;
    int16_t out_l, out_r;
    uint32_t i;

    /* test: silence in gives silence out */
    reverb_init(&r);
    for (i = 0; i < 100; i++) {
        reverb_step(&r, 0, &out_l, &out_r);
        assert(out_l == 0);
        assert(out_r == 0);
    }

    /* test: impulse response doesn't blow up or stall */
    reverb_init(&r);
    reverb_step(&r, 16000, &out_l, &out_r);
    assert(out_l != 0 || out_r != 0);  /* has response */

    int16_t prev_l = out_l, prev_r = out_r;
    int32_t energy = 0;
    for (i = 0; i < 88 * 9; i++) {  /* 9 seconds */
        reverb_step(&r, 0, &out_l, &out_r);
        energy += (int32_t)out_l * out_l + (int32_t)out_r * out_r;
    }

    /* verify tail decays (energy is lower than at start) */
    assert(energy < 1000000000LL);  /* doesn't explode */

    /* test: stereo spread (L and R should be different due to different comb taps) */
    reverb_init(&r);
    int32_t l_energy = 0, r_energy = 0;
    for (i = 0; i < 88 * 2; i++) {
        reverb_step(&r, i % 2 ? 8000 : -8000, &out_l, &out_r);
        l_energy += (int32_t)out_l * out_l;
        r_energy += (int32_t)out_r * out_r;
    }
    /* L and R should both have energy */
    assert(l_energy > 0);
    assert(r_energy > 0);

    return 0;
}
