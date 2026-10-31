/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure synthesis helpers for the audio ISR and the voice tuning (impressionism.c): the sine shape, the phase step
 * table, note-to-step conversion and the slow pitch drift. Integers only, no platform dependency, so
 * tests/synth_test.c runs them on the host. Phase is a 32-bit accumulator: one full cycle is 2^32 steps. */
#pragma once
#include <stdint.h>

/* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
static inline int32_t sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;               /* 4x(1-|x|), Q15 */
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15; /* + 0.225 (y|y| - y) */
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* phase step per frame for MIDI 120..131 (C9..B9), f * 2^32 / 44100; lower octaves shift right */
static const uint32_t INC_C9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

/* MIDI note -> phase step per frame. Clamped to 0..120: the table's reach (INC_C9 >> 10..0). */
static inline uint32_t note_to_inc(int32_t note)
{
    if (note < 0) note = 0;
    if (note > 120) note = 120;
    return INC_C9[(uint32_t)note % 12u] >> (10u - (uint32_t)note / 12u);
}

/* slow pitch drift for one voice, once per block: inc is its phase step, lfo is sine() of its drift LFO (+-32767).
   Depth: about inc * lfo / 2^22, i.e. about +-13.5 cents at lfo = 32767 (1200 * log2(1 + 32767 / 2^22)).
   The shifts are 16 and 6, not 10 and 12: with >> 10, (inc >> 10) * 32767 overflows int32 above ~689 Hz, and the
   largest inc used (note 120, 815363807) keeps (inc >> 16) * 32767 at about 4.1e8. */
static inline int32_t pitch_drift(uint32_t inc, int32_t lfo)
{
    return ((int32_t)(inc >> 16) * lfo) >> 6;
}

