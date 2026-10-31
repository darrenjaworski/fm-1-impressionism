/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
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

    /* test: impulse doesn't blow up, processes stably */
    reverb_init(&r);
    int overflow_count = 0;
    for (i = 0; i < 3000; i++) {
        reverb_step(&r, i == 0 ? 16000 : 0, &out_l, &out_r);
        /* check no overflow/underflow */
        if (out_l < -15000 || out_l > 15000) overflow_count++;
        if (out_r < -15000 || out_r > 15000) overflow_count++;
    }
    assert(overflow_count == 0);  /* no clipping */

    /* test: signal circulates and produces output (needs time for 1.3s comb delay) */
    reverb_init(&r);
    int32_t l_sum = 0, r_sum = 0;
    for (i = 0; i < 2000; i++) {
        reverb_step(&r, (i % 2) ? 4000 : -4000, &out_l, &out_r);
        l_sum += out_l;
        r_sum += out_r;
    }
    /* after 2000 steps, should have seen reverb output */
    assert(l_sum != 0 || r_sum != 0);

    return 0;
}
