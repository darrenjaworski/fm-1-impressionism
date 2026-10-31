/* SPDX-License-Identifier: GPL-3.0-only */
/* Schroeder reverb: 4 parallel damped combs → 2 series allpasses, mono in, stereo out. T60 ~7s */

#ifndef REVERB_H
#define REVERB_H

#include <stdint.h>
#include <string.h>

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

static inline void reverb_init(reverb_t *r) { memset(r, 0, sizeof(*r)); }

static inline void reverb_step(reverb_t *r, int32_t in, int16_t *out_l, int16_t *out_r)
{
    in >>= 2;  /* attenuate input */

    /* Comb 1 */
    int32_t c1 = (int32_t)r->buf1[r->pos1];
    int32_t d1 = (c1 * 127 + r->damp1) >> 7;
    r->damp1 = d1;
    int32_t fb1 = in + ((d1 * 32767) >> 15);
    r->buf1[r->pos1] = (int16_t)(fb1 > 32767 ? 32767 : fb1 < -32767 ? -32767 : fb1);
    r->pos1 = (r->pos1 + 1) % COMB1_DELAY;

    /* Comb 2 */
    int32_t c2 = (int32_t)r->buf2[r->pos2];
    int32_t d2 = (c2 * 127 + r->damp2) >> 7;
    r->damp2 = d2;
    int32_t fb2 = in + ((d2 * 32767) >> 15);
    r->buf2[r->pos2] = (int16_t)(fb2 > 32767 ? 32767 : fb2 < -32767 ? -32767 : fb2);
    r->pos2 = (r->pos2 + 1) % COMB2_DELAY;

    /* Comb 3 */
    int32_t c3 = (int32_t)r->buf3[r->pos3];
    int32_t d3 = (c3 * 127 + r->damp3) >> 7;
    r->damp3 = d3;
    int32_t fb3 = in + ((d3 * 32767) >> 15);
    r->buf3[r->pos3] = (int16_t)(fb3 > 32767 ? 32767 : fb3 < -32767 ? -32767 : fb3);
    r->pos3 = (r->pos3 + 1) % COMB3_DELAY;

    /* Comb 4 */
    int32_t c4 = (int32_t)r->buf4[r->pos4];
    int32_t d4 = (c4 * 127 + r->damp4) >> 7;
    r->damp4 = d4;
    int32_t fb4 = in + ((d4 * 32767) >> 15);
    r->buf4[r->pos4] = (int16_t)(fb4 > 32767 ? 32767 : fb4 < -32767 ? -32767 : fb4);
    r->pos4 = (r->pos4 + 1) % COMB4_DELAY;

    /* Sum combs */
    int32_t comb_sum = (d1 + d2 + d3 + d4) >> 2;

    /* Allpass 1 */
    int32_t ap1_in = (int32_t)r->ap1[r->ap1_pos];
    int32_t ap1_out = ap1_in + ((comb_sum - ap1_in) >> 1);
    r->ap1[r->ap1_pos] = (int16_t)(ap1_out > 32767 ? 32767 : ap1_out < -32767 ? -32767 : ap1_out);
    r->ap1_pos = (r->ap1_pos + 1) % ALLPASS1_DELAY;

    /* Allpass 2 */
    int32_t ap2_in = (int32_t)r->ap2[r->ap2_pos];
    int32_t ap2_out = ap2_in + ((ap1_out - ap2_in) >> 1);
    r->ap2[r->ap2_pos] = (int16_t)(ap2_out > 32767 ? 32767 : ap2_out < -32767 ? -32767 : ap2_out);
    r->ap2_pos = (r->ap2_pos + 1) % ALLPASS2_DELAY;

    /* Stereo: mix different combs for width */
    int32_t l = (ap2_out + d1 + d3) >> 2;
    int32_t r_out = (ap2_out + d2 + d4) >> 2;

    *out_l = (int16_t)(l > 12000 ? 12000 : l < -12000 ? -12000 : l);
    *out_r = (int16_t)(r_out > 12000 ? 12000 : r_out < -12000 ? -12000 : r_out);
}

#endif
