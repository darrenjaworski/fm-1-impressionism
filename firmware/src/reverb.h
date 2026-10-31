/* SPDX-License-Identifier: GPL-3.0-only */
/* Schroeder reverb: 4 parallel damped combs → 2 series allpasses, mono in, stereo out. T60 ~7s */

#ifndef REVERB_H
#define REVERB_H

#include <stdint.h>

#define COMB1_DELAY 1277u
#define COMB2_DELAY 1597u
#define COMB3_DELAY 1913u
#define COMB4_DELAY 2027u
#define ALLPASS1_DELAY 225u
#define ALLPASS2_DELAY 441u
#define REVERB_TOTAL_SAMPLES (COMB1_DELAY + COMB2_DELAY + COMB3_DELAY + COMB4_DELAY + ALLPASS1_DELAY + ALLPASS2_DELAY)

typedef struct {
    int16_t buf1[COMB1_DELAY], buf2[COMB2_DELAY], buf3[COMB3_DELAY], buf4[COMB4_DELAY];
    int16_t ap1[ALLPASS1_DELAY], ap2[ALLPASS2_DELAY];
    uint32_t pos1, pos2, pos3, pos4, ap1_pos, ap2_pos;
    int32_t damp1, damp2, damp3, damp4;
} reverb_t;

static inline void reverb_init(reverb_t *r)
{
    uint32_t i;
    for (i = 0; i < COMB1_DELAY; i++) r->buf1[i] = 0;
    for (i = 0; i < COMB2_DELAY; i++) r->buf2[i] = 0;
    for (i = 0; i < COMB3_DELAY; i++) r->buf3[i] = 0;
    for (i = 0; i < COMB4_DELAY; i++) r->buf4[i] = 0;
    for (i = 0; i < ALLPASS1_DELAY; i++) r->ap1[i] = 0;
    for (i = 0; i < ALLPASS2_DELAY; i++) r->ap2[i] = 0;
    r->pos1 = r->pos2 = r->pos3 = r->pos4 = r->ap1_pos = r->ap2_pos = 0;
    r->damp1 = r->damp2 = r->damp3 = r->damp4 = 0;
}

static inline void reverb_step(reverb_t *r, int32_t in, int16_t *out_l, int16_t *out_r)
{
    in >>= 2;  /* attenuate input */

    /* Comb 1: read delayed sample, apply damping, add input, write back, output */
    int32_t c1_rd = (int32_t)r->buf1[r->pos1];
    int32_t c1_damp = ((c1_rd * 63) >> 7) + ((r->damp1 * 64) >> 7);  /* damp: 0.5 * delayed + 0.5 * state */
    r->damp1 = c1_damp;
    int32_t c1_new = in + ((c1_damp * 960) >> 10);  /* feedback gain ~0.938 for T60 ~7s */
    r->buf1[r->pos1] = (int16_t)(c1_new > 32767 ? 32767 : c1_new < -32767 ? -32767 : c1_new);
    r->pos1 = (r->pos1 + 1) % COMB1_DELAY;

    /* Comb 2 */
    int32_t c2_rd = (int32_t)r->buf2[r->pos2];
    int32_t c2_damp = ((c2_rd * 63) >> 7) + ((r->damp2 * 64) >> 7);
    r->damp2 = c2_damp;
    int32_t c2_new = in + ((c2_damp * 960) >> 10);
    r->buf2[r->pos2] = (int16_t)(c2_new > 32767 ? 32767 : c2_new < -32767 ? -32767 : c2_new);
    r->pos2 = (r->pos2 + 1) % COMB2_DELAY;

    /* Comb 3 */
    int32_t c3_rd = (int32_t)r->buf3[r->pos3];
    int32_t c3_damp = ((c3_rd * 63) >> 7) + ((r->damp3 * 64) >> 7);
    r->damp3 = c3_damp;
    int32_t c3_new = in + ((c3_damp * 960) >> 10);
    r->buf3[r->pos3] = (int16_t)(c3_new > 32767 ? 32767 : c3_new < -32767 ? -32767 : c3_new);
    r->pos3 = (r->pos3 + 1) % COMB3_DELAY;

    /* Comb 4 */
    int32_t c4_rd = (int32_t)r->buf4[r->pos4];
    int32_t c4_damp = ((c4_rd * 63) >> 7) + ((r->damp4 * 64) >> 7);
    r->damp4 = c4_damp;
    int32_t c4_new = in + ((c4_damp * 960) >> 10);
    r->buf4[r->pos4] = (int16_t)(c4_new > 32767 ? 32767 : c4_new < -32767 ? -32767 : c4_new);
    r->pos4 = (r->pos4 + 1) % COMB4_DELAY;

    /* Sum comb outputs */
    int32_t comb_sum = (c1_damp + c2_damp + c3_damp + c4_damp) >> 2;

    /* Allpass 1 */
    int32_t ap1_rd = (int32_t)r->ap1[r->ap1_pos];
    int32_t ap1_out = ap1_rd + ((comb_sum - ap1_rd) >> 1);
    r->ap1[r->ap1_pos] = (int16_t)(ap1_out > 32767 ? 32767 : ap1_out < -32767 ? -32767 : ap1_out);
    r->ap1_pos = (r->ap1_pos + 1) % ALLPASS1_DELAY;

    /* Allpass 2 */
    int32_t ap2_rd = (int32_t)r->ap2[r->ap2_pos];
    int32_t ap2_out = ap2_rd + ((ap1_out - ap2_rd) >> 1);
    r->ap2[r->ap2_pos] = (int16_t)(ap2_out > 32767 ? 32767 : ap2_out < -32767 ? -32767 : ap2_out);
    r->ap2_pos = (r->ap2_pos + 1) % ALLPASS2_DELAY;

    /* Stereo out: mix different combs for L/R width */
    int32_t l = (ap2_out + c1_damp + c3_damp) >> 2;
    int32_t r_out = (ap2_out + c2_damp + c4_damp) >> 2;

    *out_l = (int16_t)(l > 10000 ? 10000 : l < -10000 ? -10000 : l);
    *out_r = (int16_t)(r_out > 10000 ? 10000 : r_out < -10000 ? -10000 : r_out);
}

#endif
