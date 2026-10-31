/* SPDX-License-Identifier: GPL-3.0-only */
/* The synthesis helpers (firmware/src/synth.h): the sine shape, the phase-step table, note_to_inc and pitch_drift.
 * No platform dependency: $CC -o build/host/synth_test tests/synth_test.c && build/host/synth_test
 * Literals were worked out by hand or with a separate Python check of the same arithmetic (see each comment). */
#include <stdio.h>
#include "../firmware/src/synth.h"
static int fails;
static void check(int ok, const char *what) { printf("synth: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    int32_t lfo;
    int32_t bad_sign = 0, bad_mono = 0, bad_exact = 0;
    uint32_t n;
    int64_t diff;
    uint32_t prev;

    /* sine: a full cycle over 2^32. 0 is 0; a quarter cycle is the peak, 32767 (the refined parabola reaches 32768 at
       the peak and is clamped to 32767); three quarters is the trough, -32767. */
    check(sine(0u) == 0, "sine at phase 0 is 0");
    check(sine(0x40000000u) == 32767, "sine at a quarter cycle (2^30) is the peak, 32767");
    check(sine(0xC0000000u) == -32767, "sine at three quarters (3 * 2^30) is the trough, -32767");

    /* note_to_inc(69) is A4 = 440 Hz. The exact step is 440 * 2^32 / 44100 = 42852281.41, so 42852281 (floor). The
       table holds 12 values rounded to whole units and then shifted right, so the result may be off by one unit. */
    diff = (int64_t)note_to_inc(69) - 42852281;
    check(diff >= -1 && diff <= 1, "note_to_inc(69) is 440 Hz, 42852281 within the table's rounding (+-1 unit)");

    /* the clamp: notes below 0 act as 0, notes above 120 act as 120 (the table's reach).
       note 0: INC_C9[0] >> 10 = 815363807 >> 10 = 796253.  note 120: INC_C9[0] >> 0 = 815363807. */
    check(note_to_inc(-5) == 796253u && note_to_inc(0) == 796253u, "note_to_inc clamps below 0 to note 0 (796253)");
    check(note_to_inc(120) == 815363807u, "note_to_inc(120) is the table's top, 815363807 (C9, 8372 Hz)");
    check(note_to_inc(200) == 815363807u, "note_to_inc clamps above 120 to 120");

    /* the step rises with the note over the whole range the table covers */
    prev = note_to_inc(0);
    for (n = 1; n <= 120u; n++) {
        uint32_t cur = note_to_inc((int32_t)n);
        if (cur <= prev) bad_mono = 1;
        prev = cur;
    }
    check(!bad_mono, "note_to_inc rises strictly from note 0 to note 120");

    /* pitch_drift at the largest step the engine uses (note 120, 815363807), full swing of the drift LFO.
       Literals: (815363807 >> 16) = 12441; 12441 * 32767 = 407654247; >> 6 = 6369597 (up) and -6369598 (down: the
       arithmetic shift floors a negative value). */
    check(pitch_drift(815363807u, 32767) == 6369597, "pitch_drift at the top step, lfo +32767, is +6369597");
    check(pitch_drift(815363807u, -32767) == -6369598, "pitch_drift at the top step, lfo -32767, is -6369598");

    /* against the spec: depth = inc * lfo / 2^22. Only the 16-bit truncation of inc can move it, by at most
       65535 * 32767 / 2^22 = 512 units. Checked with 64-bit arithmetic here, which the firmware does not use. */
    for (lfo = -32767; lfo <= 32767; lfo += 97) {
        int64_t exact = (int64_t)815363807 * lfo / 4194304;
        int64_t got = pitch_drift(815363807u, lfo);
        int64_t err = got - exact;
        if (err > 512 || err < -512) bad_exact = 1;
    }
    check(!bad_exact, "pitch_drift at the top step stays within 512 units of inc * lfo / 2^22, across the LFO range");

    /* never the wrong sign: no drift at lfo 0, positive for positive lfo, negative for negative lfo, and it only
       grows with lfo */
    check(pitch_drift(815363807u, 0) == 0, "pitch_drift with lfo 0 is 0");
    prev = 0;
    for (lfo = -32767; lfo <= 32767; lfo += 97) {
        int32_t dr = pitch_drift(815363807u, lfo);
        if (lfo > 0 && dr <= 0) bad_sign = 1;
        if (lfo < 0 && dr >= 0) bad_sign = 1;
        if (lfo > -32767 && dr < pitch_drift(815363807u, lfo - 97)) bad_sign = 1;
    }
    check(!bad_sign, "pitch_drift has the sign of lfo at the top step, and grows with lfo");

    printf("synth: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
