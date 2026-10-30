/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine's pure logic (firmware/src/planing.h): scales, chord templates, the planing picker and
 * the voice trigger scheduler. No platform dependency, so this runs with the plain system compiler (ROADMAP.md
 * v1): $CC -o build/host/planing_test tests/planing_test.c && build/host/planing_test */
#include <stdio.h>
#include "../firmware/src/planing.h"
static int fails;
static void check(int ok, const char *what) { printf("planing: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    /* Seam A: scale tables. Whole-tone is 6 notes a whole tone (2 semitones) apart; pentatonic (major) is the
       usual 0,2,4,7,9. Both wrap at the octave, including for negative degrees. */
    check(scale_degree_to_semitone(SCALE_WHOLETONE, 0) == 0, "whole-tone degree 0 is the root");
    check(scale_degree_to_semitone(SCALE_WHOLETONE, 1) == 2, "whole-tone degree 1 is a whole tone up");
    check(scale_degree_to_semitone(SCALE_WHOLETONE, 5) == 10, "whole-tone degree 5 is the last step before the octave");
    check(scale_degree_to_semitone(SCALE_WHOLETONE, 6) == 12, "whole-tone degree 6 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_WHOLETONE, -1) == -2, "whole-tone degree -1 wraps to the octave below");

    check(scale_degree_to_semitone(SCALE_PENTATONIC, 0) == 0, "pentatonic degree 0 is the root");
    check(scale_degree_to_semitone(SCALE_PENTATONIC, 1) == 2, "pentatonic degree 1");
    check(scale_degree_to_semitone(SCALE_PENTATONIC, 4) == 9, "pentatonic degree 4 is the last step before the octave");
    check(scale_degree_to_semitone(SCALE_PENTATONIC, 5) == 12, "pentatonic degree 5 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_PENTATONIC, -1) == -3, "pentatonic degree -1 wraps to the octave below");

    /* Seam B: chord templates (ROADMAP.md v1): maj7 +4,+7,+11; add9 +4,+7,+14, above whatever root the drone
       holds. Fixed intervals, independent of the current scale (literal planing, not re-harmonizing). */
    check(chord_voice_semitone(CHORD_MAJ7, 0) == 4, "maj7 voice 0 is a major third above the root");
    check(chord_voice_semitone(CHORD_MAJ7, 1) == 7, "maj7 voice 1 is a fifth above the root");
    check(chord_voice_semitone(CHORD_MAJ7, 2) == 11, "maj7 voice 2 is a major seventh above the root");
    check(chord_voice_semitone(CHORD_ADD9, 0) == 4, "add9 voice 0 is a major third above the root");
    check(chord_voice_semitone(CHORD_ADD9, 1) == 7, "add9 voice 1 is a fifth above the root");
    check(chord_voice_semitone(CHORD_ADD9, 2) == 14, "add9 voice 2 is a ninth (an octave + a second) above the root");

    /* Seam C: the planing picker. Design choice not explicit in ROADMAP.md, flagged here: "density" (how often
       voices retrigger) drives the scheduler (Seam D below), while "drift rate" drives how FAR each retrigger
       moves the root (the step size of this random walk) -- the two panel knobs split cleanly that way. */
    {
        uint32_t rng = 1;
        int32_t prev = 0, i, min_step = 999, max_step = -999, saw_maj7 = 0, saw_add9 = 0, bad_templ = 0;
        for (i = 0; i < 2000; i++) {
            plan_t p = plan_next(SCALE_WHOLETONE, prev, 0u, &rng);
            int32_t step = p.degree - prev;
            if (step < min_step) min_step = step;
            if (step > max_step) max_step = step;
            if (p.templ != CHORD_MAJ7 && p.templ != CHORD_ADD9) bad_templ = 1;
            saw_maj7 |= p.templ == CHORD_MAJ7;
            saw_add9 |= p.templ == CHORD_ADD9;
            prev = p.degree;
        }
        check(!bad_templ, "plan_next always returns a valid template id, over 2000 picks");
        check(min_step >= -1 && max_step <= 1 && min_step != max_step,
              "drift_rate 0: steps stay within +-1 degree and are not all the same");
        check(saw_maj7 && saw_add9, "both chord templates turn up over 2000 picks");
    }
    {
        uint32_t rng = 1;
        int32_t prev = 0, i, min_step = 999, max_step = -999;
        for (i = 0; i < 2000; i++) {
            plan_t p = plan_next(SCALE_WHOLETONE, prev, 3u, &rng);
            int32_t step = p.degree - prev;
            if (step < min_step) min_step = step;
            if (step > max_step) max_step = step;
            prev = p.degree;
        }
        check(max_step - min_step > 2, "drift_rate 3 ranges over a wider step size than drift_rate 0");
    }
    {
        uint32_t rng_a = 42, rng_b = 42;
        plan_t a = plan_next(SCALE_PENTATONIC, 0, 1u, &rng_a);
        plan_t b = plan_next(SCALE_PENTATONIC, 0, 1u, &rng_b);
        check(a.degree == b.degree && a.templ == b.templ, "plan_next is deterministic given the same seed");
    }

    /* Seam D: the voice trigger scheduler. density 0..3 (ENC_ALGORITHM, ROADMAP.md): higher density means a
       shorter, and still randomised, wait until the next retrigger. */
    {
        uint32_t rng = 7, d, i;
        uint32_t avg[4];
        for (d = 0; d < 4; d++) {
            uint32_t lo = 0xFFFFFFFFu, hi = 0, sum = 0;
            for (i = 0; i < 2000; i++) {
                uint32_t ms = next_trigger_ticks(d, &rng);
                if (ms < lo) lo = ms;
                if (ms > hi) hi = ms;
                sum += ms / 2000u;                           /* (avoids overflow; a touch of rounding is fine) */
            }
            check(lo > 0 && hi > lo, "next_trigger_ticks: a density level has a real, non-degenerate range");
            avg[d] = sum;
        }
        check(avg[0] > avg[1] && avg[1] > avg[2] && avg[2] > avg[3],
              "average wait time drops as density rises, 0 (sparsest) through 3 (densest)");
    }

    printf("planing: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
