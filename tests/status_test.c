/* SPDX-License-Identifier: GPL-3.0-only */
/* The status-text overlay's pure logic (firmware/src/status.h): the text drawn under the lily pond. No platform
 * dependency, so this runs with the plain system compiler:
 * $CC -o build/host/status_test tests/status_test.c && build/host/status_test */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/status.h"
static int fails;
static void check(int ok, const char *what) { printf("status: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* the screen's status row is 240 px wide in FONT_S; every line must fit in the 29 characters that fit on it */
#define LINE_MAX 29u

int main(void)
{
    char s[32];
    uint32_t scale, drone, density, drift, color, worst_top = 0, worst_knobs = 0, long_line = 0;

    /* every scale x drone x density combination fits on the top row */
    for (scale = 0; scale < 6u; scale++)
        for (drone = 0; drone < 2u; drone++)
            for (density = 0; density < 4u; density++) {
                uint32_t n;
                status_top(s, scale, drone, density);
                n = (uint32_t)strlen(s);
                if (n > worst_top) worst_top = n;
                if (n > LINE_MAX) long_line = 1;
            }
    check(!long_line, "every top line (scale x drone x density) is at most 29 characters");
    check(worst_top == 25u, "the longest top line is 25 characters (MIXOLYDIAN, DRONE OFF, D3 is the widest)");

    /* every drift x colour combination fits on the knob row */
    long_line = 0;
    for (drift = 0; drift < 4u; drift++)
        for (color = 0; color <= 31u; color++) {
            uint32_t n;
            status_knobs(s, drift, color);
            n = (uint32_t)strlen(s);
            if (n > worst_knobs) worst_knobs = n;
            if (n > LINE_MAX) long_line = 1;
        }
    check(!long_line, "every knob line (drift x colour) is at most 29 characters");
    check(worst_knobs == 18u, "the longest knob line is 18 characters (DRIFT 3  COLOUR 31)");

    /* literal strings: the text the screen shows, unchanged from before the overflow fix */
    status_top(s, 1u, 1u, 0u);
    check(strcmp(s, "PENTATONIC  DRONE ON  D0") == 0, "top: PENTATONIC, drone on, density 0 reads PENTATONIC  DRONE ON  D0");
    status_top(s, 2u, 0u, 3u);
    check(strcmp(s, "MINOR PENT  DRONE OFF  D3") == 0, "top: MINOR PENT (not MINOR PENTATONIC), drone off, density 3");
    status_top(s, 5u, 0u, 2u);
    check(strcmp(s, "DORIAN  DRONE OFF  D2") == 0, "top: DORIAN, drone off, density 2");
    status_top(s, 0u, 1u, 1u);
    check(strcmp(s, "WHOLE-TONE  DRONE ON  D1") == 0, "top: WHOLE-TONE, drone on, density 1");
    status_knobs(s, 0u, 12u);
    check(strcmp(s, "DRIFT 0  COLOUR 12") == 0, "knobs: DRIFT 0  COLOUR 12");
    status_knobs(s, 3u, 31u);
    check(strcmp(s, "DRIFT 3  COLOUR 31") == 0, "knobs: DRIFT 3  COLOUR 31 (two-digit colour at its top)");
    status_knobs(s, 2u, 0u);
    check(strcmp(s, "DRIFT 2  COLOUR 0") == 0, "knobs: colour 0 is one digit");

    printf("status: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
