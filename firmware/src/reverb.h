/* SPDX-License-Identifier: GPL-3.0-only */
/* Schroeder reverb: 4 parallel damped combs → 2 series feedback combs, mono in, stereo out.
   T60 about 2.7 s (T30 method, 200 Hz burst, tests/reverb_test.c; 2.1 to 3.8 s across 100-900 Hz and window sizes),
   2.1 s at 1.8 kHz. Permanent DC offset of about -18 from rounding. */

#ifndef REVERB_H
#define REVERB_H

#include <stdint.h>

#define COMB1_DELAY 1277u
#define COMB2_DELAY 1597u
#define COMB3_DELAY 1913u
#define COMB4_DELAY 2027u
#define FEEDBACK1_DELAY 225u   /* 2nd stage feedback comb, not a true allpass */
#define FEEDBACK2_DELAY 441u

typedef struct {
    int16_t buf1[COMB1_DELAY], buf2[COMB2_DELAY], buf3[COMB3_DELAY], buf4[COMB4_DELAY];
    int16_t fb1[FEEDBACK1_DELAY], fb2[FEEDBACK2_DELAY];
    uint32_t pos1, pos2, pos3, pos4, fb1_pos, fb2_pos;
    int32_t damp1, damp2, damp3, damp4;
} reverb_t;

static inline void clamp_i16(int16_t *out, int32_t val)
{
    *out = (int16_t)(val > 32767 ? 32767 : val < -32767 ? -32767 : val);
}

static inline int32_t comb_step(int16_t *buf, uint32_t *pos, uint32_t delay,
                                 int32_t *damp, int32_t in)
{
    int32_t rd = (int32_t)buf[*pos];
    int32_t d = ((rd * 63) >> 7) + ((*damp * 64) >> 7);
    *damp = d;
    int32_t new = in + ((d * 960) >> 10);
    clamp_i16(&buf[*pos], new);
    *pos = *pos + 1;
    if (*pos == delay) *pos = 0;
    return d;
}

static inline int32_t feedback_comb(int16_t *buf, uint32_t *pos, uint32_t delay,
                                     int32_t in)
{
    int32_t rd = (int32_t)buf[*pos];
    int32_t out = rd + ((in - rd) >> 1);
    clamp_i16(&buf[*pos], out);
    *pos = *pos + 1;
    if (*pos == delay) *pos = 0;
    return out;
}

static inline void reverb_step(reverb_t *r, int32_t in, int16_t *out_l, int16_t *out_r)
{
    in >>= 2;  /* attenuate input */

    int32_t c1 = comb_step(r->buf1, &r->pos1, COMB1_DELAY, &r->damp1, in);
    int32_t c2 = comb_step(r->buf2, &r->pos2, COMB2_DELAY, &r->damp2, in);
    int32_t c3 = comb_step(r->buf3, &r->pos3, COMB3_DELAY, &r->damp3, in);
    int32_t c4 = comb_step(r->buf4, &r->pos4, COMB4_DELAY, &r->damp4, in);

    int32_t comb_sum = (c1 + c2 + c3 + c4) >> 2;
    int32_t fb1 = feedback_comb(r->fb1, &r->fb1_pos, FEEDBACK1_DELAY, comb_sum);
    int32_t fb2 = feedback_comb(r->fb2, &r->fb2_pos, FEEDBACK2_DELAY, fb1);

    int32_t l = (fb2 + c1 + c3) >> 2;
    int32_t r_out = (fb2 + c2 + c4) >> 2;

    *out_l = (int16_t)(l > 10000 ? 10000 : l < -10000 ? -10000 : l);
    *out_r = (int16_t)(r_out > 10000 ? 10000 : r_out < -10000 ? -10000 : r_out);
}

#endif
