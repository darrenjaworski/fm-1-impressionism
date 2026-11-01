/* SPDX-License-Identifier: GPL-3.0-only */
/* The status text under the lily pond (impressionism.c's draw_status): the scale, drone and density on the top
 * row, the knob values on the next, then voice leading and the sleep slot. Pure functions only, no platform dependency (no fm1_*, no screen calls, no
 * floats) -- so tests/status_test.c checks every line with the system compiler. Integers only, per docs/RULES.md.
 * Each line must fit the 240 px row in FONT_S: 29 characters at most (tests/status_test.c). */
#pragma once
#include <stdint.h>

/* scale names, indexed by planing.h's SCALE_* values (same order). This header does not include planing.h: the
   caller passes the scale as an index. "MINOR PENT" is short enough to fit the row with the drone and density. */
static const char *const SCALE_NAME[6] = { "WHOLE-TONE", "PENTATONIC", "MINOR PENT", "LYDIAN", "MIXOLYDIAN", "DORIAN" };

/* writes the string p at s[i], returns the index after it (no terminator) */
static uint32_t str_put(char *s, uint32_t i, const char *p)
{
    while (*p) s[i++] = *p++;
    return i;
}

/* writes v in decimal at s[i], returns the index after it (no terminator); v is 0..99 here */
static uint32_t put_dec(char *s, uint32_t i, uint32_t v)
{
    if (v >= 10u) s[i++] = (char)('0' + v / 10u % 10u);
    s[i++] = (char)('0' + v % 10u);
    return i;
}

/* the top row, e.g. "PENTATONIC  DRONE ON  D0": scale, drone state, then ENC_ALGORITHM's density. s needs 26 bytes */
static void status_top(char *s, uint32_t scale, uint32_t drone_on, uint32_t density)
{
    uint32_t i = str_put(s, 0, SCALE_NAME[scale]);
    i = str_put(s, i, "  ");
    i = str_put(s, i, drone_on ? "DRONE ON" : "DRONE OFF");
    i = str_put(s, i, "  D");
    i = put_dec(s, i, density);
    s[i] = 0;
}

/* the knob row, e.g. "DRIFT 0  COLOUR 12": ENC_KNOB1 drift, then ENC_KNOB2 colour (0..31). s needs 19 bytes */
static void status_knobs(char *s, uint32_t drift, uint32_t color)
{
    uint32_t i = str_put(s, 0, "DRIFT ");
    i = put_dec(s, i, drift);
    i = str_put(s, i, "  COLOUR ");
    i = put_dec(s, i, color);
    s[i] = 0;
}

/* the bottom row, e.g. "VOICE 2  SLEEP --": ENC_KNOB3 voice leading (0..3), then the sleep timer's slot, which stays "--"
   until it is built. s needs 18 bytes */
static void status_bottom(char *s, uint32_t voice_lead)
{
    uint32_t i = str_put(s, 0, "VOICE ");
    i = put_dec(s, i, voice_lead);
    i = str_put(s, i, "  SLEEP --");
    s[i] = 0;
}
