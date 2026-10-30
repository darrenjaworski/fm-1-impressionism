/* SPDX-License-Identifier: GPL-3.0-only */
/* The cheap "poor man's reverb" (ROADMAP.md, dreamier character): a feed-forward four-tap delay on the mono sum,
 * returned into L and R on alternating taps for width. Pure logic and no platform dependency (the ring buffer is
 * passed in), so tests/diffuse_test.c runs it with the system compiler. Integers only, no feedback (nothing can
 * ring on or run away), a few multiplies per frame: sized for the audio ISR budget (docs/RULES.md). */
#pragma once
#include <stdint.h>

#define DIFF_LEN 4096u                           /* ring size, a power of two; 8 KB as int16 */
#define DIFF_MASK (DIFF_LEN - 1u)
#define DIFF_WET 6000                            /* per-tap return gain, Q15 (~0.18) */
#define DIFF_WET_MAX 12000                       /* worst case |output|: two taps per channel at full scale */

/* delays in frames at 44.1 kHz (~23, 37, 53, 71 ms), prime so the echoes never line up into a comb */
static const uint16_t DIFF_TAP[4] = { 1013u, 1637u, 2339u, 3137u };

static int16_t diff_clamp16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : (int16_t)x; }

/* push one mono frame, get this frame's wet return for each channel */
static void diffuse_step(int16_t *buf, uint32_t *pos, int32_t in, int32_t *wet_l, int32_t *wet_r)
{
    uint32_t p = *pos;
    buf[p & DIFF_MASK] = diff_clamp16(in);
    *wet_l = ((int32_t)buf[(p - DIFF_TAP[0]) & DIFF_MASK] + buf[(p - DIFF_TAP[2]) & DIFF_MASK]) * DIFF_WET >> 15;
    *wet_r = ((int32_t)buf[(p - DIFF_TAP[1]) & DIFF_MASK] + buf[(p - DIFF_TAP[3]) & DIFF_MASK]) * DIFF_WET >> 15;
    *pos = p + 1u;
}
