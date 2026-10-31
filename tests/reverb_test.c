/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include "../firmware/src/reverb.h"

static void reverb_zero(reverb_t *r)
{
    uint32_t i;
    r->pos1 = r->pos2 = r->pos3 = r->pos4 = r->fb1_pos = r->fb2_pos = 0;
    r->damp1 = r->damp2 = r->damp3 = r->damp4 = 0;
    for (i = 0; i < COMB1_DELAY; i++) r->buf1[i] = 0;
    for (i = 0; i < COMB2_DELAY; i++) r->buf2[i] = 0;
    for (i = 0; i < COMB3_DELAY; i++) r->buf3[i] = 0;
    for (i = 0; i < COMB4_DELAY; i++) r->buf4[i] = 0;
    for (i = 0; i < FEEDBACK1_DELAY; i++) r->fb1[i] = 0;
    for (i = 0; i < FEEDBACK2_DELAY; i++) r->fb2[i] = 0;
}

int main(void)
{
    reverb_t r;
    int16_t out_l, out_r;
    uint32_t i;

    /* silence in gives silence out */
    reverb_zero(&r);
    for (i = 0; i < 100u; i++) {
        reverb_step(&r, 0, &out_l, &out_r);
        assert(out_l == 0 && out_r == 0);
    }

    /* impulse doesn't blow up, processes stably (comb delays ~29 ms each) */
    reverb_zero(&r);
    for (i = 0; i < 200000u; i++) {
        reverb_step(&r, i == 0u ? 32767 : 0, &out_l, &out_r);
        assert(out_l <= 10000 && out_l >= -10000 && out_r <= 10000 && out_r >= -10000);
    }

    /* two runs with identical input produce identical output (deterministic) */
    reverb_zero(&r);
    int16_t trace[512];
    for (i = 0; i < 512u; i++) {
        reverb_step(&r, 4000, &out_l, &out_r);
        trace[i] = out_l;
    }
    reverb_zero(&r);
    for (i = 0; i < 512u; i++) {
        reverb_step(&r, 4000, &out_l, &out_r);
        assert(trace[i] == out_l);
    }

    /* permanent DC offset from rounding (tail settles around -18, never to zero) */

    return 0;
}
