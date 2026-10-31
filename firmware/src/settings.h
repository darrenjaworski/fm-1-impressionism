/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings that survive a power-off (ROADMAP.md): five one-byte fields, saved as they are in flash. The layout and
 * the field order must not change, or settings saved by an earlier build stop loading. Pure: no platform dependency,
 * integers only, so tests/engine_test.c checks them on the host. */
#pragma once
#include <stdint.h>

#define SETTINGS_SCALE_COUNT 6u                 /* planing.h's SCALE_COUNT; engine.h checks that the two match */

typedef struct {
    uint8_t scale;                              /* ENC_SELECT: 0 .. SETTINGS_SCALE_COUNT - 1 */
    uint8_t density;                            /* ENC_ALGORITHM: 0 .. 3 */
    uint8_t drift;                              /* ENC_KNOB1: 0 .. 3 */
    uint8_t color;                              /* ENC_KNOB2: 0 .. 31 */
    uint8_t drone_on;                           /* BTN_ARP: 0 or 1 */
} settings_t;

/* the dreamier start: pentatonic, sparse, slow, darker, drone on. Written field by field (no struct copy, no memcpy) */
static inline void settings_set_defaults(settings_t *s)
{
    s->scale = 1u;                              /* SCALE_PENTATONIC */
    s->density = 0u;
    s->drift = 0u;
    s->color = 12u;
    s->drone_on = 1u;
}

/* 1 when every field is inside its range: a loaded block that fails this is replaced by the defaults */
static inline int settings_valid(const settings_t *s)
{
    return s->scale < SETTINGS_SCALE_COUNT && s->density <= 3u && s->drift <= 3u && s->color <= 31u &&
           s->drone_on <= 1u;
}

/* a clamped knob: value moved by e detents (either way, any size), held inside 0..max */
static inline uint8_t knob_apply(uint32_t value, int32_t e, uint32_t max)
{
    int32_t d = (int32_t)value + e;
    return (uint8_t)(d < 0 ? 0 : d > (int32_t)max ? (int32_t)max : d);
}

/* every setting and the chord template in one word; the screen redraws when it changes. Each field has its own bits,
   wide enough for its largest value: density bits 0-1, drift 2-3, drone 4, colour 5-9, chord template 10-12, scale 13-15 */
static inline uint32_t settings_state_word(const settings_t *s, uint32_t chord_templ)
{
    return (uint32_t)s->density | (uint32_t)s->drift << 2 | (uint32_t)s->drone_on << 4 | (uint32_t)s->color << 5 |
           chord_templ << 10 | (uint32_t)s->scale << 13;
}
