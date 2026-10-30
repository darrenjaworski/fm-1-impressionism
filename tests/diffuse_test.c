/* SPDX-License-Identifier: GPL-3.0-only */
/* The cheap diffuser's pure logic (firmware/src/diffuse.h): a feed-forward multi-tap delay, no platform
 * dependency. $CC -o build/host/diffuse_test tests/diffuse_test.c && build/host/diffuse_test */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/diffuse.h"
static int fails;
static void check(int ok, const char *what) { printf("diffuse: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

static int16_t buf[DIFF_LEN];

int main(void)
{
    uint32_t pos = 0, i;
    int32_t wl, wr;

    /* silence in, silence out */
    {
        int nonzero = 0;
        memset(buf, 0, sizeof buf);
        for (i = 0; i < 2u * DIFF_LEN; i++) {
            diffuse_step(buf, &pos, 0, &wl, &wr);
            if (wl || wr) nonzero = 1;
        }
        check(!nonzero, "silence in gives silence out");
    }

    /* an impulse comes back exactly at each tap's delay, on the channel that tap is wired to, and nowhere else */
    {
        uint32_t t, hits_l = 0, hits_r = 0, stray = 0;
        memset(buf, 0, sizeof buf);
        pos = 0;
        for (t = 0; t < DIFF_LEN; t++) {
            diffuse_step(buf, &pos, t == 0 ? 20000 : 0, &wl, &wr);
            if (t == DIFF_TAP[0] || t == DIFF_TAP[2]) { if (wl > 0 && wr == 0) hits_l++; else stray++; }
            else if (t == DIFF_TAP[1] || t == DIFF_TAP[3]) { if (wr > 0 && wl == 0) hits_r++; else stray++; }
            else if (wl || wr) stray++;
        }
        check(hits_l == 2u, "taps 0 and 2 return the impulse on the left, at their delays");
        check(hits_r == 2u, "taps 1 and 3 return the impulse on the right, at their delays");
        check(!stray, "no output at any other time (feed-forward: no feedback, nothing rings on)");
    }

    /* full-scale input never overflows the wet bus's bound, in either polarity */
    {
        int32_t peak = 0;
        memset(buf, 0, sizeof buf);
        pos = 0;
        for (i = 0; i < 3u * DIFF_LEN; i++) {
            diffuse_step(buf, &pos, (i / 7u) & 1u ? 32767 : -32767, &wl, &wr);
            if (wl > peak) peak = wl;
            if (-wl > peak) peak = -wl;
            if (wr > peak) peak = wr;
            if (-wr > peak) peak = -wr;
        }
        check(peak <= DIFF_WET_MAX, "wet output stays within DIFF_WET_MAX for full-scale input");
        check(DIFF_WET_MAX <= 13000, "and that bound leaves headroom under the dry mix");
    }

    /* an input beyond int16 is clamped into the buffer, not wrapped */
    {
        memset(buf, 0, sizeof buf);
        pos = 0;
        diffuse_step(buf, &pos, 100000, &wl, &wr);
        check(buf[0] == 32767, "an oversized input is clamped to +32767, not wrapped");
    }

    /* the longest tap fits the buffer */
    check(DIFF_TAP[0] < DIFF_LEN && DIFF_TAP[1] < DIFF_LEN && DIFF_TAP[2] < DIFF_LEN && DIFF_TAP[3] < DIFF_LEN,
          "every tap is shorter than the ring buffer");

    printf("diffuse: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
