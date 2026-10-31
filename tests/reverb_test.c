/* SPDX-License-Identifier: GPL-3.0-only */
/* The Schroeder reverb (firmware/src/reverb.h): silence, boundedness, determinism, the measured decay, and a sustained
 * full-scale input. No platform dependency and no libm (the stimulus uses synth.h's sine), so the plain system
 * compiler runs it: $CC -o build/host/reverb_test tests/reverb_test.c && build/host/reverb_test */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/reverb.h"
#include "../firmware/src/synth.h"              /* sine(): the stimulus generator only, not under test here */
static int fails;
static void check(int ok, const char *what) { printf("reverb: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

#define OUT_MAX 250000u                         /* 5.7 s at 44.1 kHz: the sustained run needs 5.5 s */
static reverb_t r;                              /* static: about 15 KB, too big for the stack */
static int16_t out[OUT_MAX];                    /* the left output of the last run_tone() */
static int32_t out_peak;                        /* largest |output| of the last run_tone() */

static void reverb_zero(reverb_t *r)
{
    uint32_t i;
    r->pos1 = r->pos2 = r->pos3 = r->pos4 = r->fb1_pos = r->fb2_pos = 0;
    r->damp1 = r->damp2 = r->damp3 = r->damp4 = 0;
    for (i = 0; i < COMB1_DELAY; i++) r->buf1[i] = 0;
    for (i = 0; i < COMB2_DELAY; i++) r->buf2[i] = 0;
    for (i = 0; i < COMB3_DELAY; i++) r->buf3[i] = 0;
    for (i = 0; i < COMB4_DELAY; i++) r->buf4[i] = 0;
    for (i = 0; i < FEEDBACK1_DELAY; i++) r->fb1[i] = 0;
    for (i = 0; i < FEEDBACK2_DELAY; i++) r->fb2[i] = 0;
}

/* the reverb's left output into out[] for n_total samples: a sine of phase step inc and peak amp for the first n_tone
   samples (input = sine(ph) * amp >> 15), then silence. The reverb attenuates its input by 4 on entry. */
static void run_tone(uint32_t inc, int32_t amp, uint32_t n_tone, uint32_t n_total)
{
    uint32_t i, ph = 0;
    int16_t l, rr;
    reverb_zero(&r);
    out_peak = 0;
    for (i = 0; i < n_total; i++) {
        int32_t in = i < n_tone ? (sine(ph) * amp) >> 15 : 0;
        int32_t a;
        ph += inc;
        reverb_step(&r, in, &l, &rr);
        out[i] = l;
        a = l < 0 ? -(int32_t)l : (int32_t)l;
        if (a > out_peak) out_peak = a;
    }
}

/* mean-square of out[] over one window, with its own DC removed (the reverb's rounding leaves a DC of about -18) */
static double window_energy(uint32_t start, uint32_t len)
{
    uint32_t k;
    double mean = 0, s = 0;
    for (k = 0; k < len; k++) mean += out[start + k];
    mean /= len;
    for (k = 0; k < len; k++) {
        double v = out[start + k] - mean;
        s += v * v;
    }
    return s / len;
}

int main(void)
{
    uint32_t i;
    int bad;

    /* silence in gives silence out */
    reverb_zero(&r);
    bad = 0;
    for (i = 0; i < 100u; i++) {
        int16_t out_l, out_r;
        reverb_step(&r, 0, &out_l, &out_r);
        if (out_l != 0 || out_r != 0) bad = 1;
    }
    check(!bad, "silence in gives silence out");

    /* impulse doesn't blow up, processes stably (comb delays ~29 ms each) */
    reverb_zero(&r);
    bad = 0;
    for (i = 0; i < 200000u; i++) {
        int16_t out_l, out_r;
        reverb_step(&r, i == 0u ? 32767 : 0, &out_l, &out_r);
        if (!(out_l <= 10000 && out_l >= -10000 && out_r <= 10000 && out_r >= -10000)) bad = 1;
    }
    check(!bad, "an impulse decays and stays within +-10000 (no blow-up) over 200000 samples");

    /* two runs with identical input produce identical output (deterministic) */
    {
        int16_t trace[512];
        int16_t out_l, out_r;
        reverb_zero(&r);
        for (i = 0; i < 512u; i++) {
            reverb_step(&r, 4000, &out_l, &out_r);
            trace[i] = out_l;
        }
        reverb_zero(&r);
        bad = 0;
        for (i = 0; i < 512u; i++) {
            reverb_step(&r, 4000, &out_l, &out_r);
            if (trace[i] != out_l) bad = 1;
        }
        check(!bad, "identical input gives identical output on a second run (deterministic)");
    }

    /* the decay, measured: a 200 Hz sine burst of 0.2 s (input peak 4000), then silence. T60 by the T30 method: the
       time from -5 dB to -35 dB, doubled, on the energy of 0.05 s windows with DC removed. The reference is the window
       that starts when the burst stops (window 4, at 0.2 s). Measured on this build: t-5 at window 9, t-35 at window 36,
       so 27 windows = 1.35 s, and T60 = 2.7 s. The same method gives 2.1 s to 3.8 s for 100..900 Hz at 10..50 ms
       windows, and 2.1 s at 1800 Hz, so this band guards this stimulus; it is not a claim about every input. The
       documented 4.5 s (old reverb.h comment) is not reproduced. */
    {
        const uint32_t win = 2205u, n_total = 110250u;  /* 0.05 s windows, 2.5 s in all: 50 windows */
        const uint32_t ref = 8820u / 2205u;             /* window 4, 0.2 s */
        double e_ref, t60;
        uint32_t w, t5 = 0, t35 = 0;
        run_tone(19478310u, 4000, 8820u, n_total);      /* 200 Hz: 19478310 = round(200 * 2^32 / 44100) */
        e_ref = window_energy(ref * win, win);
        for (w = ref + 1; w < n_total / win; w++) {
            double e = window_energy(w * win, win);
            if (t5 == 0 && e <= 0.31622776601683794 * e_ref) t5 = w;      /* 10^(-5/10) */
            if (t35 == 0 && e <= 0.00031622776601683794 * e_ref) t35 = w; /* 10^(-35/10) */
        }
        t60 = (double)(t35 - t5) * 0.1;                 /* 2 x (t35 - t5) windows x 0.05 s */
        printf("reverb: measured T60 by the T30 method, 200 Hz burst: %.2f s\n", t60);
        check(t5 != 0 && t35 != 0 && t60 >= 2.3 && t60 <= 3.1,
              "T60 by the T30 method (-5 to -35 dB, doubled) for a 200 Hz burst is 2.3..3.1 s");
    }

    /* sustained full scale: a 32767 sine at 200 Hz for 3 s, then silence. The output stays far below the +-10000 clamp
       (measured peak 6191), holds a steady level while the input runs (1.0..2.5 s), and falls 40 dB below that level
       within 2 s of the input stopping: the 0.5 s window at 5.0..5.5 s (measured 17 RMS against 2600 sustained). */
    {
        double e_sus, e_after;
        run_tone(19478310u, 32767, 44100u * 3u, 44100u * 5u + 22050u);   /* to 5.5 s: fits out[] */
        check(out_peak < 10000, "a sustained full-scale input stays under the +-10000 clamp");
        e_sus = window_energy(44100u, 66150u);          /* 1.0 s to 2.5 s */
        e_after = window_energy(44100u * 5u, 22050u);   /* 5.0 s to 5.5 s */
        check(e_after < e_sus * 0.0001, "after a sustained full-scale input stops, the output is 40 dB down within 2 s");
    }

    /* permanent DC offset from rounding (tail settles around -18, never to zero): a comment in reverb.h; measured as
       about -18.6 on the same kind of run, not checked as a number here */

    printf("reverb: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
