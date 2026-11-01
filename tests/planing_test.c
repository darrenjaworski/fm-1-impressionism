/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine's pure logic (firmware/src/planing.h): scales, chord templates, the planing picker and
 * the voice trigger scheduler. No platform dependency, so this runs with the plain system compiler (ROADMAP.md
 * v1): $CC -o build/host/planing_test tests/planing_test.c && build/host/planing_test */
#include <stdio.h>
#include "../firmware/src/planing.h"
static int fails;
static void check(int ok, const char *what) { printf("planing: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* 1 when the template's three voices sit exactly a, b and c semitones above the root */
static int voices_are(uint32_t templ, int32_t a, int32_t b, int32_t c)
{
    return chord_voice_semitone(templ, 0) == a && chord_voice_semitone(templ, 1) == b &&
           chord_voice_semitone(templ, 2) == c;
}

/* bit t is set when template t fits this scale degree */
static uint32_t fit_mask(uint32_t scale, int32_t degree)
{
    uint32_t t, m = 0;
    for (t = 0; t < CHORD_TEMPLATE_COUNT; t++)
        if (chord_fits(scale, degree, t)) m |= 1u << t;
    return m;
}

int main(void)
{
    /* Seam A: scale tables. Whole-tone is 6 notes a whole tone (2 semitones) apart; pentatonic (major) is the
       usual 0,2,4,7,9. Every scale wraps at the octave, including for negative degrees. */
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

    /* minor pentatonic 0,3,5,7,10; Lydian 0,2,4,6,7,9,11; Mixolydian 0,2,4,5,7,9,10; Dorian 0,2,3,5,7,9,10 */
    check(scale_degree_to_semitone(SCALE_MINOR_PENTATONIC, 1) == 3, "minor pentatonic degree 1 is a minor third up");
    check(scale_degree_to_semitone(SCALE_MINOR_PENTATONIC, 4) == 10, "minor pentatonic degree 4 is a minor seventh up");
    check(scale_degree_to_semitone(SCALE_MINOR_PENTATONIC, 5) == 12, "minor pentatonic degree 5 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_MINOR_PENTATONIC, -1) == -2, "minor pentatonic degree -1 wraps to the octave below");

    check(scale_degree_to_semitone(SCALE_LYDIAN, 3) == 6, "Lydian degree 3 is the raised fourth (+6)");
    check(scale_degree_to_semitone(SCALE_LYDIAN, 6) == 11, "Lydian degree 6 is the major seventh");
    check(scale_degree_to_semitone(SCALE_LYDIAN, 7) == 12, "Lydian degree 7 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_LYDIAN, -1) == -1, "Lydian degree -1 wraps to the seventh below");

    check(scale_degree_to_semitone(SCALE_MIXOLYDIAN, 3) == 5, "Mixolydian degree 3 is the fourth (+5)");
    check(scale_degree_to_semitone(SCALE_MIXOLYDIAN, 6) == 10, "Mixolydian degree 6 is the minor seventh");
    check(scale_degree_to_semitone(SCALE_MIXOLYDIAN, 7) == 12, "Mixolydian degree 7 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_MIXOLYDIAN, -1) == -2, "Mixolydian degree -1 wraps to the seventh below");

    check(scale_degree_to_semitone(SCALE_DORIAN, 2) == 3, "Dorian degree 2 is the minor third");
    check(scale_degree_to_semitone(SCALE_DORIAN, 6) == 10, "Dorian degree 6 is the minor seventh");
    check(scale_degree_to_semitone(SCALE_DORIAN, 7) == 12, "Dorian degree 7 wraps to the octave above");
    check(scale_degree_to_semitone(SCALE_DORIAN, -1) == -2, "Dorian degree -1 wraps to the seventh below");

    /* Seam B: chord templates (ROADMAP.md): maj7 +4,+7,+11; add9 +4,+7,+14, above whatever root the drone holds.
       Fixed intervals, independent of the current scale (literal planing, not re-harmonizing). */
    check(chord_voice_semitone(CHORD_MAJ7, 0) == 4, "maj7 voice 0 is a major third above the root");
    check(chord_voice_semitone(CHORD_MAJ7, 1) == 7, "maj7 voice 1 is a fifth above the root");
    check(chord_voice_semitone(CHORD_MAJ7, 2) == 11, "maj7 voice 2 is a major seventh above the root");
    check(chord_voice_semitone(CHORD_ADD9, 0) == 4, "add9 voice 0 is a major third above the root");
    check(chord_voice_semitone(CHORD_ADD9, 1) == 7, "add9 voice 1 is a fifth above the root");
    check(chord_voice_semitone(CHORD_ADD9, 2) == 14, "add9 voice 2 is a ninth (an octave + a second) above the root");
    check(voices_are(CHORD_SUS2, 2, 7, 12), "sus2 is +2,+7,+12 above the root");
    check(voices_are(CHORD_SUS4, 5, 7, 12), "sus4 is +5,+7,+12 above the root");
    check(voices_are(CHORD_SIX9, 4, 9, 14), "6/9 is +4,+9,+14 above the root (ROADMAP.md's corrected voicing)");
    check(voices_are(CHORD_QUARTAL, 5, 10, 15), "quartal is +5,+10,+15 above the root (ROADMAP.md's corrected voicing)");
    check(voices_are(CHORD_9NO5, 4, 10, 14), "9no5 is +4,+10,+14 above the root");
    check(voices_are(CHORD_AUG9, 4, 8, 14), "aug9 is +4,+8,+14 above the root");

    /* scale_step: ENC_SELECT moves the scale by any signed number of detents, wrapping through all SCALE_COUNT */
    check(scale_step(SCALE_WHOLETONE, 1) == SCALE_PENTATONIC, "one detent up from whole-tone is pentatonic");
    check(scale_step(SCALE_WHOLETONE, -1) == SCALE_DORIAN, "one detent down from whole-tone wraps to Dorian");
    check(scale_step(SCALE_DORIAN, 1) == SCALE_WHOLETONE, "one detent up from Dorian wraps to whole-tone");
    check(scale_step(SCALE_PENTATONIC, 2) == SCALE_LYDIAN, "two detents up from pentatonic reach Lydian");
    check(scale_step(SCALE_PENTATONIC, -3) == SCALE_MIXOLYDIAN, "three detents down from pentatonic wrap to Mixolydian");
    check(scale_step(SCALE_WHOLETONE, 7) == SCALE_PENTATONIC, "seven detents up from whole-tone wrap past the end");
    check(scale_step(SCALE_LYDIAN, -13) == 2u, "thirteen detents down from Lydian wrap twice and land on minor pentatonic");

    /* chord_fits: a template fits a degree when all three voices land on notes of the scale (by pitch class).
       Fit depends on the degree, not just the scale. fit_mask has bit t set for template t. */
    check(chord_fits(SCALE_LYDIAN, 0, CHORD_MAJ7), "Lydian degree 0 fits maj7");
    check(!chord_fits(SCALE_LYDIAN, 0, CHORD_QUARTAL), "Lydian degree 0 does not fit quartal");
    check(!chord_fits(SCALE_LYDIAN, 1, CHORD_MAJ7), "Lydian degree 1 does not fit maj7 (its 7th lands on a flat second)");
    check(fit_mask(SCALE_LYDIAN, 0) == 0x17u, "Lydian degree 0 fits exactly maj7, add9, sus2 and 6/9");
    check(fit_mask(SCALE_LYDIAN, 1) == 0x5Eu, "Lydian degree 1 fits exactly add9, sus2, sus4, 6/9 and 9no5");
    check(fit_mask(SCALE_PENTATONIC, 2) == 0x20u, "pentatonic degree 2 fits only quartal");
    check(fit_mask(SCALE_DORIAN, 5) == 0x20u, "Dorian degree 5 fits only quartal");
    check(fit_mask(SCALE_WHOLETONE, 0) == 0xC0u, "whole-tone degree 0 fits only 9no5 and aug9");
    check(fit_mask(SCALE_LYDIAN, -1) == 0x28u, "Lydian degree -1 (a negative degree) fits sus4 and quartal");
    {
        int32_t d, any_9no5 = 0;
        for (d = 0; d <= 4; d++)
            if (chord_fits(SCALE_PENTATONIC, d, CHORD_9NO5)) any_9no5 = 1;
        check(!any_9no5, "9no5 does not fit any pentatonic degree");
    }
    {
        uint32_t s, t;
        int32_t d, none = 0;
        for (s = 0; s < SCALE_COUNT; s++)
            for (d = -12; d <= 12; d++) {
                int any = 0;
                for (t = 0; t < CHORD_TEMPLATE_COUNT; t++)
                    if (chord_fits(s, d, t)) any = 1;
                if (!any) none = 1;
            }
        check(!none, "every degree of every scale, from -12 to +12, has at least one fitting chord");
    }

    /* pick_chord: a uniform pick among the fitting templates only */
    {
        uint32_t rng = 1, i, saw = 0;
        for (i = 0; i < 2000u; i++) saw |= 1u << pick_chord(SCALE_LYDIAN, 0, &rng);
        check((saw & ~0x17u) == 0u, "pick_chord at Lydian degree 0 never returns a template that does not fit");
        check(saw == 0x17u, "pick_chord at Lydian degree 0 reaches all four fitting templates over 2000 picks");
    }
    {
        uint32_t rng = 9, i, saw = 0;
        for (i = 0; i < 2000u; i++) saw |= 1u << pick_chord(SCALE_WHOLETONE, 0, &rng);
        check(saw == 0xC0u, "pick_chord at whole-tone degree 0 picks only 9no5 and aug9, and both turn up");
    }
    {
        uint32_t rng = 3, i, saw = 0;
        for (i = 0; i < 200u; i++) saw |= 1u << pick_chord(SCALE_PENTATONIC, 2, &rng);
        check(saw == 0x20u, "pick_chord at a single-option degree always returns that option");
    }

    /* Seam C: the planing picker (plan_next): a degree-only random walk. Design choice not explicit in ROADMAP.md,
       flagged here: "density" (how often voices retrigger) drives the scheduler (Seam D below), while "drift rate"
       drives how FAR each retrigger moves the root (the step size of this random walk). */
    {
        uint32_t rng = 1;
        int32_t prev = 0, i, min_step = 999, max_step = -999;
        for (i = 0; i < 2000; i++) {
            int32_t next = plan_next(prev, 0u, &rng);
            int32_t step = next - prev;
            if (step < min_step) min_step = step;
            if (step > max_step) max_step = step;
            prev = next;
        }
        check(min_step >= -1 && max_step <= 1 && min_step != max_step,
              "drift_rate 0: steps stay within +-1 degree and are not all the same");
    }
    {
        uint32_t rng = 1;
        int32_t prev = 0, i, min_step = 999, max_step = -999;
        for (i = 0; i < 2000; i++) {
            int32_t next = plan_next(prev, 3u, &rng);
            int32_t step = next - prev;
            if (step < min_step) min_step = step;
            if (step > max_step) max_step = step;
            prev = next;
        }
        check(max_step - min_step > 2, "drift_rate 3 ranges over a wider step size than drift_rate 0");
    }
    {
        uint32_t rng_a = 42, rng_b = 42;
        int32_t a = plan_next(0, 1u, &rng_a);
        int32_t b = plan_next(0, 1u, &rng_b);
        check(a == b, "plan_next is deterministic given the same seed");
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

    /* Seam E: the register limit (clamp_register). The drone's home stays within -12..+12 semitones of the scale root,
       a chord root within -12..+19. A degree moves toward 0 until its semitone is in range; one already in range is
       unchanged. Literals below were worked out by hand and checked with a separate Python version of the scale table. */
    {
        uint32_t s;
        int32_t d, bad_home = 0, bad_root = 0, bad_keep = 0, no_fit = 0;
        for (s = 0; s < SCALE_COUNT; s++)
            for (d = -40; d <= 40; d++) {
                int32_t h = clamp_register(s, d, -12, 12), r = clamp_register(s, d, -12, 19);
                int32_t sh = scale_degree_to_semitone(s, h), sr = scale_degree_to_semitone(s, r);
                int32_t sd = scale_degree_to_semitone(s, d);
                if (sh < -12 || sh > 12) bad_home = 1;
                if (sr < -12 || sr > 19) bad_root = 1;
                if (sd >= -12 && sd <= 12 && h != d) bad_keep = 1;
                if (sd >= -12 && sd <= 19 && r != d) bad_keep = 1;
                if (fit_mask(s, h) == 0u || fit_mask(s, r) == 0u) no_fit = 1;
            }
        check(!bad_home, "clamp_register at -12..+12: every scale, degrees -40..+40, lands in range");
        check(!bad_root, "clamp_register at -12..+19: every scale, degrees -40..+40, lands in range");
        check(!bad_keep, "clamp_register: a degree already in range comes back unchanged");
        check(!no_fit, "clamp_register: every clamped degree still has a fitting chord template");
    }
    check(clamp_register(SCALE_PENTATONIC, 10, -12, 12) == 5, "pentatonic degree 10 (+24 st) clamps to degree 5 (+12 st)");
    check(clamp_register(SCALE_PENTATONIC, -10, -12, 12) == -5, "pentatonic degree -10 (-24 st) clamps to degree -5 (-12 st)");
    check(clamp_register(SCALE_WHOLETONE, 12, -12, 12) == 6, "whole-tone degree 12 (+24 st) clamps to degree 6 (+12 st)");
    check(clamp_register(SCALE_PENTATONIC, 9, -12, 19) == 8, "pentatonic degree 9 (+21 st) clamps to degree 8 (+19 st) at -12..+19");
    check(clamp_register(SCALE_PENTATONIC, 6, -12, 19) == 6, "pentatonic degree 6 (+14 st) is inside -12..+19, unchanged");
    check(clamp_register(SCALE_LYDIAN, 20, -12, 12) == 7, "Lydian degree 20 (+35 st) clamps to degree 7 (+12 st)");
    check(clamp_register(SCALE_LYDIAN, 20, -12, 19) == 11, "Lydian degree 20 (+35 st) clamps to degree 11 (+19 st) at -12..+19");
    check(clamp_register(SCALE_PENTATONIC, -8, -12, 12) == -5, "pentatonic degree -8 (-20 st) clamps to degree -5 (-12 st)");
    check(clamp_register(SCALE_DORIAN, 3, -12, 12) == 3, "Dorian degree 3 (+5 st) is inside -12..+12, unchanged");
    check(clamp_register(SCALE_PENTATONIC, 0, -12, 12) == 0, "degree 0 is its own register (the root)");

    /* voice leading: chord_shared_pitches counts the new chord's voices that land on a pitch class the outgoing chord has.
       Worked by hand, as pitch classes: sus4 on semitone 7 is {0, 2, 7}; sus2 on 0 is {0, 2, 7}; add9 on 0 is {2, 4, 7};
       6/9 on 0 is {2, 4, 9}; quartal on 0 is {3, 5, 10}; maj7 on 0 is {4, 7, 11}. */
    check(chord_shared_pitches(7, CHORD_SUS4, 0, CHORD_SUS2) == 3, "shared pitches: sus4 on 7 and sus2 on 0 are the same three notes, 3");
    check(chord_shared_pitches(7, CHORD_SUS4, 0, CHORD_ADD9) == 2, "shared pitches: sus4 on 7 and add9 on 0 share 2 and 7, so 2");
    check(chord_shared_pitches(7, CHORD_SUS4, 0, CHORD_SIX9) == 1, "shared pitches: sus4 on 7 and 6/9 on 0 share only 2, so 1");
    check(chord_shared_pitches(0, CHORD_QUARTAL, 0, CHORD_MAJ7) == 0, "shared pitches: quartal and maj7 on the same root share nothing, 0");
    check(chord_shared_pitches(-5, CHORD_SUS4, 0, CHORD_SUS2) == 3, "shared pitches: a root 5 below 0 is the same pitch classes as 7");
    check(chord_shared_pitches(19, CHORD_SUS4, 0, CHORD_SUS2) == 3, "shared pitches: a root an octave and a bit above is the same pitch classes too");
    {
        uint32_t a, b, bad = 0;
        int32_t ra, rb;
        for (a = 0; a < CHORD_TEMPLATE_COUNT; a++)
            for (b = 0; b < CHORD_TEMPLATE_COUNT; b++)
                for (ra = -12; ra <= 19; ra += 3)
                    for (rb = -12; rb <= 19; rb += 5) {
                        int32_t n = chord_shared_pitches(ra, a, rb, b);
                        if (n < 0 || n > 3 || n != chord_shared_pitches(rb, b, ra, a)) bad = 1;
                    }
        check(!bad, "shared pitches is 0..3 and the same whichever chord is called the outgoing one, for every template pair");
        check(chord_shared_pitches(4, CHORD_AUG9, 4, CHORD_AUG9) == 3, "shared pitches: a chord shares all 3 notes with itself");
    }

    /* pick_chord_led at lead 0 is exactly pick_chord: the same pick and the same random state, so the sound at lead 0
       is the sound before voice leading existed */
    {
        uint32_t sc, k, bad = 0, ra = 77u, rb = 77u;
        int32_t d;
        for (sc = 0; sc < SCALE_COUNT; sc++)
            for (d = -6; d <= 6; d++)
                for (k = 0; k < 200u; k++) {
                    uint32_t x = pick_chord(sc, d, &ra);
                    uint32_t y = pick_chord_led(sc, d, &rb, (int32_t)(k % 12u), k % CHORD_TEMPLATE_COUNT, 0u);
                    if (x != y || ra != rb) bad = 1;
                }
        check(!bad, "pick_chord_led at lead 0 returns pick_chord's pick and leaves the same random state, whatever the old chord");
    }

    /* at every lead, only a template that fits the degree is ever returned */
    {
        uint32_t sc, lead, k, bad = 0, rng = 5u;
        int32_t d;
        for (lead = 0; lead <= 3u; lead++)
            for (sc = 0; sc < SCALE_COUNT; sc++)
                for (d = -12; d <= 12; d++)
                    for (k = 0; k < 40u; k++) {
                        uint32_t t = pick_chord_led(sc, d, &rng, (int32_t)(k % 19u) - 7, k % CHORD_TEMPLATE_COUNT, lead);
                        if (t >= CHORD_TEMPLATE_COUNT || !chord_fits(sc, d, t)) bad = 1;
                    }
        check(!bad, "pick_chord_led returns only a fitting template, at every lead, scale and degree from -12 to +12");
    }
    {
        uint32_t rng = 3u, i, other = 0;
        for (i = 0; i < 500u; i++)
            if (pick_chord_led(SCALE_PENTATONIC, 2, &rng, 0, CHORD_MAJ7, 3u) != CHORD_QUARTAL) other = 1;
        check(!other, "pick_chord_led at a single-option degree still returns that option at lead 3");
    }

    /* the worked example: pentatonic, new root at degree 0 (semitone 0), outgoing sus4 on 7. Three chords fit: add9 shares
       2 notes, sus2 shares 3, 6/9 shares 1. Weights 2^(lead * shared), so the chances (worked separately) are
       lead 0: 1/3 each; lead 1: 4/14, 8/14, 2/14; lead 2: 16/84, 64/84, 4/84; lead 3: 64/584, 512/584, 8/584 */
    {
        static const double WANT[4][3] = {
            { 0.3333, 0.3333, 0.3333 }, { 0.2857, 0.5714, 0.1429 }, { 0.1905, 0.7619, 0.0476 }, { 0.1096, 0.8767, 0.0137 } };
        uint32_t lead, i, rng = 12345u, all_close = 1u;
        for (lead = 0; lead <= 3u; lead++) {
            uint32_t cnt[3] = { 0u, 0u, 0u };
            double got[3];
            for (i = 0; i < 100000u; i++) {
                uint32_t t = pick_chord_led(SCALE_PENTATONIC, 0, &rng, 7, CHORD_SUS4, lead);
                if (t == CHORD_ADD9) cnt[0]++;
                else if (t == CHORD_SUS2) cnt[1]++;
                else if (t == CHORD_SIX9) cnt[2]++;
            }
            for (i = 0; i < 3u; i++) {
                double diff;
                got[i] = cnt[i] / 100000.0;
                diff = got[i] - WANT[lead][i];
                if (diff > 0.01 || diff < -0.01) all_close = 0;
            }
            printf("planing:   lead %u: add9 %.4f, sus2 %.4f, 6/9 %.4f (want %.4f, %.4f, %.4f)\n", lead, got[0], got[1], got[2],
                   WANT[lead][0], WANT[lead][1], WANT[lead][2]);
        }
        check(all_close, "pick_chord_led's chances in the worked example are within 0.01 of the hand-worked figures, at all four leads");
    }

    /* the effect of each step, over every scale, a spread of degrees, and every old template at a spread of roots. Most
       degrees offer only 2-3 fitting templates and the old chord here is arbitrary, so the best available choice often
       keeps little and the means are low (measured 0.78, 0.92, 1.02, 1.08: steps of 0.14, 0.10 and 0.06). The engine
       test measures the same thing on real chord moves. Each step must still clear 0.05. */
    {
        double mean[4];
        uint32_t lead, sc, k, t_old, rng = 99u;
        int32_t d, r_old;
        for (lead = 0; lead <= 3u; lead++) {
            double sum = 0.0;
            uint32_t n = 0u;
            for (sc = 0; sc < SCALE_COUNT; sc++)
                for (d = -5; d <= 5; d++)
                    for (t_old = 0; t_old < CHORD_TEMPLATE_COUNT; t_old++)
                        for (r_old = 0; r_old < 12; r_old += 2)
                            for (k = 0; k < 20u; k++) {
                                uint32_t t = pick_chord_led(sc, d, &rng, r_old, t_old, lead);
                                sum += chord_shared_pitches(r_old, t_old, scale_degree_to_semitone(sc, d), t);
                                n++;
                            }
            mean[lead] = sum / n;
        }
        printf("planing:   mean shared notes per lead: %.3f, %.3f, %.3f, %.3f\n", mean[0], mean[1], mean[2], mean[3]);
        check(mean[1] - mean[0] >= 0.05 && mean[2] - mean[1] >= 0.05 && mean[3] - mean[2] >= 0.05,
              "voice leading: the mean shared notes rises by at least 0.05 at every lead step");
    }

    printf("planing: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
