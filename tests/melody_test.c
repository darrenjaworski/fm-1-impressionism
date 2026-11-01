/* SPDX-License-Identifier: GPL-3.0-only */
/* The melody (firmware/src/melody.h) on a fake clock at 1 ms steps: an hour per run, 4 seeds, all six scales and all four
 * densities, over a chord and drone path that changes. Expected values are literal: the scale sets and chord templates are
 * written out here, not copied from the code, and the worked examples are worked by hand (see their comments). The
 * phrase structure is read back from the note events alone, exactly: an intra-phrase note lasts from its ON to the next
 * ON, and its OFF sits at floor(80% of its length), so floor(80% of the gap to the next ON) equal to the ON-to-OFF time
 * means the next note is in the same phrase. No platform dependency:
 * $CC -o build/host/melody_test tests/melody_test.c && build/host/melody_test */
#include <stdio.h>
#include <stdint.h>
#include "../firmware/src/melody.h"

static int fails;
static void check(int ok, const char *what) { printf("melody: %-78s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

#define RUN_MS 3600000u                         /* one simulated hour per run: 3.6 million 1 ms steps */
#define WRAP_RUN_MS 1200000u                    /* the wrap comparison: 20 minutes, the counter wraps 20 s in */
#define EV_MAX 16000u                           /* events kept per run (an hour of melody is about 4000) */
#define PH_MAX 4096u                            /* phrases kept per run */
#define TOL_MS 2                                /* the rest and breath checks: the end of a phrase is its last note's ON + length */

/* literal scale sets, as pitch classes above C, in planing.h's order (not the code's tables) */
static const int WHOLETONE_PC[] = { 0, 2, 4, 6, 8, 10 };
static const int PENTATONIC_PC[] = { 0, 2, 4, 7, 9 };
static const int MINOR_PENT_PC[] = { 0, 3, 5, 7, 10 };
static const int LYDIAN_PC[] = { 0, 2, 4, 6, 7, 9, 11 };
static const int MIXOLYDIAN_PC[] = { 0, 2, 4, 5, 7, 9, 10 };
static const int DORIAN_PC[] = { 0, 2, 3, 5, 7, 9, 10 };
static const int *const SCALE_PC[6] = { WHOLETONE_PC, PENTATONIC_PC, MINOR_PENT_PC, LYDIAN_PC, MIXOLYDIAN_PC, DORIAN_PC };
static const uint32_t SCALE_N[6] = { 6u, 5u, 5u, 7u, 7u, 7u };

/* the eight chord templates as literal semitones above the root, in planing.h's order */
static const int CHORD_ST[8][3] = { { 4, 7, 11 }, { 4, 7, 14 }, { 2, 7, 12 }, { 5, 7, 12 },
                                    { 4, 9, 14 }, { 5, 10, 15 }, { 4, 10, 14 }, { 4, 8, 14 } };

static const uint32_t SEEDS[4] = { 1u, 0x9E3779B9u, 0xDEADBEEFu, 0x2545F491u };
static const uint32_t PATHS[4] = { 77u, 0x1234567u, 0xC0FFEEu, 0x51ED270Bu };   /* the chord path's own seeds */

typedef struct { uint32_t t, kind, note, level, drone_pc, top_pc; } ev_t;   /* t: ms since the run started */

static int in_scale(uint32_t scale, uint32_t note)
{
    uint32_t i, pc = note % 12u;
    for (i = 0; i < SCALE_N[scale]; i++)
        if ((uint32_t)SCALE_PC[scale][i] == pc) return 1;
    return 0;
}

/* the pitch class of a scale degree, from the literal table (any integer degree; the octave is ignored) */
static uint32_t lit_root_pc(uint32_t scale, int32_t d)
{
    int32_t n = (int32_t)SCALE_N[scale];
    int32_t i = ((d % n) + n) % n;
    return (uint32_t)SCALE_PC[scale][i];
}

/* a chord at this degree fits when each of its three tones is in the scale */
static int lit_fits(uint32_t scale, int32_t d, uint32_t t)
{
    uint32_t root = lit_root_pc(scale, d), v;
    for (v = 0; v < 3u; v++)
        if (!in_scale(scale, (root + (uint32_t)CHORD_ST[t][v]) % 12u)) return 0;
    return 1;
}

/* the chord path's generator: its own xorshift32, not the code's, so the path is independent of the melody */
static uint32_t path_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* the chord moves by -2..+2 degrees, to a template that fits there (kept within -8..8) */
static void path_pick_chord(uint32_t scale, uint32_t *rs, int32_t *cdeg, uint32_t *templ)
{
    uint32_t tries, t, nf, fits[8];
    for (tries = 0; tries < 1000u; tries++) {
        int32_t d = *cdeg + (int32_t)(path_next(rs) % 5u) - 2;
        if (d > 8 || d < -8) d = 0;
        nf = 0;
        for (t = 0; t < 8u; t++)
            if (lit_fits(scale, d, t)) fits[nf++] = t;
        *cdeg = d;
        if (nf) {
            *templ = fits[path_next(rs) % nf];
            return;
        }
    }
    check(0, "chord path: a fitting chord was found within 1000 draws");
}

/* the motif's invariants: 3..6 notes, step[0] 0, span at most MELODY_SPAN_MAX, |step| at most 7, durations 1..2 and last 3..4 */
static uint32_t motif_ok(const melody_motif_t *mo)
{
    uint32_t i;
    if (mo->len < 3u || mo->len > MELODY_MOTIF_MAX) return 0u;
    if (mo->step[0] != 0) return 0u;
    if (melody_span(mo->step, mo->len) > MELODY_SPAN_MAX) return 0u;
    for (i = 0; i < mo->len; i++) {
        if (mo->step[i] > 7 || mo->step[i] < -7) return 0u;
        if (i + 1u < mo->len && (mo->dur[i] < 1u || mo->dur[i] > 2u)) return 0u;
    }
    if (mo->dur[mo->len - 1u] < 3u || mo->dur[mo->len - 1u] > 4u) return 0u;
    return 1u;
}

/* one run on the fake clock: a melody started at `start`, ms steps of tick and of the chord path. Logs the ON and OFF
   events, with the drone and the chord's top tone (literal pitch classes) at the tick that sent them. Returns the count. */
static uint32_t sim(ev_t *log, uint32_t mseed, uint32_t pseed, uint32_t scale, uint32_t density, uint32_t start, uint32_t ms,
                    uint32_t *motif_bad, uint32_t *motif_changes)
{
    static melody_t m;
    uint32_t rs = pseed ? pseed : 1u, n = 0u, k, now, next_chord, next_home, templ = 0u, what, note = 0u, level = 0u;
    uint32_t snap_len = 0u, snap_step[MELODY_MOTIF_MAX], snap_dur[MELODY_MOTIF_MAX], i, same;
    int32_t cdeg = 0, home = 0, drone_pc, top_pc;
    melody_init(&m, mseed, start);
    path_pick_chord(scale, &rs, &cdeg, &templ);
    next_chord = start + 5000u + path_next(&rs) % 10001u;
    next_home = start + 60000u + path_next(&rs) % 120001u;
    for (k = 0; k < ms; k++) {
        now = start + k;
        if (melody_due(now, next_chord)) {
            path_pick_chord(scale, &rs, &cdeg, &templ);
            next_chord = now + 5000u + path_next(&rs) % 10001u;
        }
        if (melody_due(now, next_home)) {
            home += (int32_t)(path_next(&rs) % 3u) - 1;
            if (home > 8 || home < -8) home = 0;
            next_home = now + 60000u + path_next(&rs) % 120001u;
        }
        drone_pc = (int32_t)lit_root_pc(scale, home);
        top_pc = (int32_t)((lit_root_pc(scale, cdeg) + (uint32_t)CHORD_ST[templ][2]) % 12u);
        what = melody_tick(&m, now, scale, cdeg, templ, drone_pc, density, &note, &level);
        if (what == MELODY_NONE) continue;
        if (n >= EV_MAX) {
            check(0, "run: the event log has room (EV_MAX)");
            return n;
        }
        log[n].t = k;
        log[n].kind = what;
        log[n].note = note;
        log[n].level = what == MELODY_ON ? level : 0u;
        log[n].drone_pc = (uint32_t)drone_pc;
        log[n].top_pc = (uint32_t)top_pc;
        n++;
        if (what == MELODY_ON) {          /* the motif, checked at each note on: its invariants, and whether it changed */
            if (!motif_ok(&m.motif)) (*motif_bad)++;
            same = m.motif.len == snap_len;
            for (i = 0; same && i < m.motif.len; i++)
                if (m.motif.step[i] != (int8_t)snap_step[i] || m.motif.dur[i] != (uint8_t)snap_dur[i]) same = 0;
            if (!same) {
                (*motif_changes)++;
                snap_len = m.motif.len;
                for (i = 0; i < m.motif.len; i++) {
                    snap_step[i] = (uint32_t)(int32_t)m.motif.step[i];
                    snap_dur[i] = m.motif.dur[i];
                }
            }
        }
    }
    return n;
}

/* per density, over all the runs */
typedef struct {
    uint64_t sounding_ms, notes, rest_sum, rests, breaths;
    uint32_t rest_min, rest_max, breath_min, breath_max, runs;
} dagg_t;
static dagg_t agg[4];

typedef struct {
    uint32_t notes, bad_scale, bad_reg, bad_level, double_on, orphan_off, not_increasing, short_note;
    uint32_t phrases, echoes, returns, ends_bad, anchor_bad, echo_bad, identical, rest_bad, breath_bad, echo_ends_drone;
    uint32_t order_bad, level_mix, first_bad, motif_bad, motif_changes, open_at_end, pair_bad;
} stats_t;

static uint32_t ph_first[PH_MAX], ph_count[PH_MAX];

/* the phrase's last note's end: its ON plus its length, which floor(80% of the length) pins to within one ms */
static uint32_t end_est(const ev_t *log, uint32_t note_idx)
{
    uint32_t d = log[2u * note_idx + 1u].t - log[2u * note_idx].t;
    return log[2u * note_idx].t + (5u * d + 3u) / 4u;     /* the smallest length with floor(80% of it) == d */
}

/* the rhythm of a phrase (its notes' lengths) equal to another's: same notes, same gaps, same last note's OFF */
static uint32_t same_rhythm(const ev_t *log, uint32_t fa, uint32_t ca, uint32_t fb, uint32_t cb)
{
    uint32_t q;
    if (ca != cb) return 0u;
    for (q = 0; q + 1u < ca; q++)
        if (log[2u * (fa + q + 1u)].t - log[2u * (fa + q)].t != log[2u * (fb + q + 1u)].t - log[2u * (fb + q)].t) return 0u;
    return log[2u * (fa + ca - 1u) + 1u].t - log[2u * (fa + ca - 1u)].t == log[2u * (fb + cb - 1u) + 1u].t - log[2u * (fb + cb - 1u)].t;
}

/* the checks on one run's log: the events alternate and move forward, each note is in its scale and register and at a level
   the spec names, each phrase is cut from the next by the ON-to-OFF rule, and the phrases' ends, rests and echoes are
   checked against the rules. Notes are aggregated into dagg_t for the measured numbers. */
static void check_log(const ev_t *log, uint32_t n, uint32_t scale, uint32_t density, uint32_t start_is_zero, stats_t *s)
{
    uint32_t r, nn = n / 2u, nn_on = (n + 1u) / 2u, p, np = 0u, np_eff, prev_last = 1u, q;
    uint32_t i, rest_lo = MELODY_REST_MS[density].min_ms, rest_hi = MELODY_REST_MS[density].max_ms;
    for (i = 0; i < n; i++) {                   /* ON, OFF, ON, OFF...: an ON where an OFF is due, or the reverse, is a pairing fault */
        if (i > 0 && log[i].t <= log[i - 1u].t) s->not_increasing++;
        if ((i % 2u == 0u) != (log[i].kind == MELODY_ON)) s->pair_bad++;
    }
    if (n % 2u == 1u) s->open_at_end++;
    for (r = 0; r < nn; r++) {
        uint32_t note = log[2u * r].note, level = log[2u * r].level, d = log[2u * r + 1u].t - log[2u * r].t;
        s->notes++;
        agg[density].notes++;
        agg[density].sounding_ms += d;
        if (!in_scale(scale, note)) s->bad_scale++;
        if (note < 60u || note > 86u) s->bad_reg++;
        if (level != MELODY_STATEMENT_LEVEL && level != MELODY_ECHO_LEVEL) s->bad_level++;
    }
    /* phrases: note r continues the phrase when the next ON is exactly its length on (floor(80%) of the gap = its OFF).
       The next ON may belong to a note whose OFF falls after the run ends (nn_on counts it); a phrase still open at the
       end is left out of the phrase checks (np_eff) */
    for (r = 0; r < nn; r++) {
        uint32_t last = 0u;                     /* no next ON in the log: the phrase may run past the end, so it stays open */
        if (r + 1u < nn_on) {
            uint32_t delta = log[2u * (r + 1u)].t - log[2u * r].t, d = log[2u * r + 1u].t - log[2u * r].t;
            last = delta * MELODY_OFF_PCT / 100u == d ? 0u : 1u;
            if (last) {                         /* note r ends its phrase: floor(80% of its length) >= 120 is exactly length >= 150 */
                if (d < MELODY_NOTE_MIN_MS * MELODY_OFF_PCT / 100u) s->short_note++;
            } else {                            /* an intra note: its length is the gap to the next ON */
                if (delta < MELODY_NOTE_MIN_MS) s->short_note++;
            }
        }
        if (prev_last) {
            ph_first[np] = r;
            ph_count[np] = 0u;
            np++;
            if (np == PH_MAX) {
                check(0, "run: the phrase table has room (PH_MAX)");
                return;
            }
        }
        ph_count[np - 1u]++;
        prev_last = last;
    }
    np_eff = prev_last ? np : np - 1u;         /* the last phrase is complete only if its last note ended it */
    if (nn == 0u) return;
    for (p = 0; p < np_eff; p++) {
        uint32_t f = ph_first[p], c = ph_count[p], lvl = log[2u * f].level, last = f + c - 1u;
        for (q = f; q <= last; q++)
            if (log[2u * q].level != lvl) s->level_mix++;
        if (p == 0 && start_is_zero) {
            uint32_t first = log[0].t;
            if (first < 6000u || first > 10000u) s->first_bad++;
        }
        if (lvl == MELODY_STATEMENT_LEVEL) {
            s->phrases++;
            if (p > 0 && log[2u * f].level == MELODY_STATEMENT_LEVEL) s->returns++;
            /* the first note is the anchor: the chord's top tone (step 0), and the last is neither the tonic nor the drone */
            if (log[2u * f].note % 12u != log[2u * f].top_pc) s->anchor_bad++;
            if (log[2u * last].note % 12u == 0u || log[2u * last].note % 12u == log[2u * f].drone_pc) s->ends_bad++;
        } else {
            s->echoes++;
            if (log[2u * last].note % 12u == 0u || log[2u * last].note % 12u == log[2u * f].drone_pc) s->echo_ends_drone++;
        }
        if (p > 0) {
            uint32_t pf = ph_first[p - 1u], pc = ph_count[p - 1u];
            uint32_t prev_lvl = log[2u * pf].level;
            uint32_t gap = (uint32_t)((int32_t)(log[2u * f].t - end_est(log, pf + pc - 1u)));
            if (prev_lvl == MELODY_STATEMENT_LEVEL && lvl == MELODY_ECHO_LEVEL) {
                /* the echo follows its phrase after a breath of 500-1400 ms */
                if (gap + TOL_MS < 500u || gap > 1400u + TOL_MS) s->breath_bad++;
                if (agg[density].breaths == 0u || gap < agg[density].breath_min) agg[density].breath_min = gap;
                if (gap > agg[density].breath_max) agg[density].breath_max = gap;
                agg[density].breaths++;
                /* the echo repeats the phrase before it exactly: same notes, same lengths */
                {
                    uint32_t k;
                    int same = pc == c;
                    for (k = 0; same && k < c; k++)
                        if (log[2u * (pf + k)].note != log[2u * (f + k)].note) same = 0;
                    if (!same || !same_rhythm(log, pf, pc, f, c)) s->echo_bad++;
                }
            } else if (prev_lvl == MELODY_ECHO_LEVEL && lvl == MELODY_STATEMENT_LEVEL) {
                /* after an echo: a rest, then a return */
                if (gap + TOL_MS < rest_lo || gap > rest_hi + TOL_MS) s->rest_bad++;
                agg[density].rest_sum += gap; agg[density].rests++;
                if (agg[density].rests == 1u || gap < agg[density].rest_min) agg[density].rest_min = gap;
                if (gap > agg[density].rest_max) agg[density].rest_max = gap;
            } else if (prev_lvl == MELODY_STATEMENT_LEVEL && lvl == MELODY_STATEMENT_LEVEL) {
                /* a return with no echo: a rest, then the next phrase */
                if (gap + TOL_MS < rest_lo || gap > rest_hi + TOL_MS) s->rest_bad++;
                agg[density].rest_sum += gap; agg[density].rests++;
                if (agg[density].rests == 1u || gap < agg[density].rest_min) agg[density].rest_min = gap;
                if (gap > agg[density].rest_max) agg[density].rest_max = gap;
            } else {
                s->order_bad++;                 /* an echo after an echo never happens */
            }
        }
    }
    /* consecutive statements and returns (echoes aside) never share their note lengths */
    {
        uint32_t prev = np_eff;
        for (p = 0; p < np_eff; p++) {
            if (log[2u * ph_first[p]].level != MELODY_STATEMENT_LEVEL) continue;
            if (prev < np_eff && same_rhythm(log, ph_first[prev], ph_count[prev], ph_first[p], ph_count[p])) s->identical++;
            prev = p;
        }
    }
}

/* a checked run, with its numbers added to the aggregate */
static uint32_t run_checked(uint32_t mseed, uint32_t pseed, uint32_t scale, uint32_t density, uint32_t start, uint32_t ms,
                            ev_t *log, stats_t *s)
{
    uint32_t n;
    n = sim(log, mseed, pseed, scale, density, start, ms, &s->motif_bad, &s->motif_changes);
    check_log(log, n, scale, density, start == 0u, s);
    agg[density].runs++;
    return n;
}

static void matrix_tests(void)
{
    static ev_t log[EV_MAX];
    static stats_t s;
    uint32_t si, sc, de, d;
    double silent[4];
    for (si = 0; si < 4u; si++)
        for (sc = 0; sc < 6u; sc++)
            for (de = 0; de < 4u; de++)
                run_checked(SEEDS[si], PATHS[si], sc, de, 0u, RUN_MS, log, &s);

    printf("melody: runs: 4 seeds x 6 scales x 4 densities, %u simulated minutes each; notes %llu, phrases %u, echoes %u, "
           "returns %u, motif changes %u\n", RUN_MS / 60000u, (unsigned long long)s.notes, s.phrases, s.echoes, s.returns,
           s.motif_changes);
    for (d = 0; d < 4u; d++) {
        double total = (double)RUN_MS * (double)agg[d].runs;
        double npm = (double)agg[d].notes / (total / 60000.0);
        double mean_rest = agg[d].rests ? (double)agg[d].rest_sum / (double)agg[d].rests : 0.0;
        silent[d] = 100.0 - 100.0 * (double)agg[d].sounding_ms / total;
        printf("melody: density %u: %.2f notes per minute of melody; silent %.1f%% of the time (no note on)\n", d, npm, silent[d]);
        printf("melody: density %u: %llu rests, %u..%u ms, mean %.0f ms (table %u..%u ms); %llu breaths, %u..%u ms\n", d,
               (unsigned long long)agg[d].rests, agg[d].rest_min, agg[d].rest_max, mean_rest, MELODY_REST_MS[d].min_ms,
               MELODY_REST_MS[d].max_ms, (unsigned long long)agg[d].breaths, agg[d].breath_min, agg[d].breath_max);
    }
    printf("melody: echoes that end on the drone's pitch class (counted, not a rule: an echo replays the old pitches): %u of %u\n",
           s.echo_ends_drone, s.echoes);
    check(s.notes > 0u && s.bad_scale == 0u, "every note is in its scale (literal pitch-class sets; 4 seeds x 6 scales x 4 densities)");
    check(s.bad_reg == 0u, "every note is in 60..86");
    check(s.bad_level == 0u && s.level_mix == 0u, "every level is 200 (statement, return) or 120 (echo), and a phrase has one level");
    check(s.pair_bad == 0u && s.orphan_off == 0u && s.double_on == 0u && s.not_increasing == 0u,
          "every ON is followed by its OFF before the next ON; events move forward, one per ms (ON and OFF never share a call)");
    check(s.short_note == 0u, "every note lasts at least 150 ms (exact: an intra note is the ON-to-ON gap, a last note floor(80%) >= 120)");
    check(s.ends_bad == 0u, "no statement or return ends on pitch class 0 or on the drone's pitch class (at its start)");
    check(s.anchor_bad == 0u, "each statement and return starts on the chord's top tone (pitch class of the anchor)");
    check(s.echoes > 0u && s.echo_bad == 0u, "every echo repeats the phrase before it exactly (same notes, same lengths) at level 120");
    check(s.breath_bad == 0u, "every echo follows its phrase after a breath of 500-1400 ms");
    check(s.rest_bad == 0u && agg[0].rests > 0u && agg[3].rests > 0u,
          "every rest before a return is inside the density's table range (+-2 ms), at every density");
    check(s.order_bad == 0u, "no echo follows an echo");
    check(s.identical == 0u, "no two consecutive statements or returns have identical note lengths");
    check(s.returns > 0u && s.motif_changes > 0u, "returns happen, and the motif changes (a step, or a new motif) over the runs");
    check(s.first_bad == 0u, "the opening statement comes 6-10 s after start, in every run");
    check(s.motif_bad == 0u, "the motif stays valid at every note: 3..6 notes, step 0 first, span <= 6, durations per the rules");
    check(silent[0] >= 50.0, "silence fills at least half the time at density 0 (measured, see above)");
    {
        double m0 = (double)agg[0].rest_sum / (double)agg[0].rests;
        double m3 = (double)agg[3].rest_sum / (double)agg[3].rests;
        check(m0 > m3 * 2.0, "the mean rest at density 0 is over twice the mean at density 3");
    }
}

/* the worked examples, by hand. Scale 1 is pentatonic (degrees 0 2 4 7 9 at semitones 0, 2, 4, 7, 9; degree d is
   semitone 12*(d div 5) + that). The chord is ADD9 on the root: tones 4, 7, 14, so its top tone is 14, pitch class 2.
   Pitch class 2 is first found at degree 11 (semitone 26, MIDI 48 + 26 = 74). */
static void worked_examples(void)
{
    melody_motif_t mo;
    melody_t m;
    int32_t st[MELODY_MOTIF_MAX], d0 = -1;
    uint32_t units[MELODY_MOTIF_MAX], note[MELODY_MOTIF_MAX], k, i, t, seen = 0u, ok;

    /* the motif: steps 0, -1, -2 (a falling line), units 1, 1, 3 */
    mo.len = 3u;
    mo.step[0] = 0; mo.step[1] = -1; mo.step[2] = -2;
    mo.dur[0] = 1u; mo.dur[1] = 1u; mo.dur[2] = 3u;

    k = melody_transform(&mo, MELODY_XF_INVERT, 0u, st, units);
    check(k == 3u && st[0] == 0 && st[1] == 1 && st[2] == 2 && units[0] == 1u && units[1] == 1u && units[2] == 3u,
          "INVERT: steps 0, -1, -2 come back as 0, +1, +2, the units unchanged");
    check(melody_anchor(1u, 2, &d0) && d0 == 11, "anchor: in pentatonic, pitch class 2 is degree 11 (MIDI 74)");
    check(melody_place(1u, 11, st, 3u, 9, note) && note[0] == 74u && note[1] == 76u && note[2] == 79u,
          "INVERT at degree 11, drone pitch class 9: notes 74, 76, 79 (the last is pitch class 7: no move)");

    k = melody_transform(&mo, MELODY_XF_NONE, 0u, st, units);
    check(k == 3u && st[0] == 0 && st[1] == -1 && st[2] == -2 && units[2] == 3u, "NONE: the motif comes back as it is");
    check(melody_place(1u, 11, st, 3u, 9, note) && note[0] == 74u && note[1] == 72u && note[2] == 74u,
          "NONE at degree 11, drone pitch class 9: 74, 72, then the last note 69 is pitch class 9 (the drone): up to 74");

    k = melody_transform(&mo, MELODY_XF_FRAGMENT, 2u, st, units);
    check(k == 2u && st[0] == 0 && st[1] == -1 && units[0] == 1u && units[1] == 3u,
          "FRAGMENT keeps the first 2 notes and gives the last 3 units");
    check(melody_place(1u, 11, st, 2u, 9, note) && note[0] == 74u && note[1] == 74u,
          "FRAGMENT at degree 11: 74, 72, then the last is tonic (pitch class 0): up to 74");

    /* a mutation changes exactly one interior step by one degree: from 0, 2, 1, -1 the only results are 0,3,1,-1;
       0,1,1,-1; 0,2,2,-1; 0,2,0,-1 */
    ok = 1u;
    for (t = 0; t < 200u; t++) {
        static const int C[4][4] = { { 0, 3, 1, -1 }, { 0, 1, 1, -1 }, { 0, 2, 2, -1 }, { 0, 2, 0, -1 } };
        uint32_t j, matched = 0u;
        m.motif.len = 4u;
        m.motif.step[0] = 0; m.motif.step[1] = 2; m.motif.step[2] = 1; m.motif.step[3] = -1;
        m.motif.dur[0] = 1u; m.motif.dur[1] = 1u; m.motif.dur[2] = 2u; m.motif.dur[3] = 4u;
        m.rng = 1000u + t * 7919u;
        melody_mutate_step(&m);
        for (j = 0; j < 4u; j++) {
            uint32_t q, eq = 1u;
            for (q = 0; q < 4u; q++) if (m.motif.step[q] != C[j][q]) eq = 0u;
            if (eq) { matched = 1u; seen |= 1u << j; }
        }
        if (!matched) ok = 0u;
    }
    check(ok, "mutation: one interior step moves by one degree; every result is one of the 4 literal motifs");
    check(seen == 0xFu, "mutation: over 200 draws all four literal results occur");

    /* every third change is a new motif: changes 1..6 are a step, a step, a new motif, a step, a step, a new motif */
    {
        uint32_t fresh[6], c;
        melody_t mm;
        mm.rng = 4242u;
        melody_new_motif(&mm);
        mm.mutations = 0u;
        for (c = 0; c < 6u; c++) fresh[c] = melody_change_motif(&mm);
        check(fresh[0] == 0u && fresh[1] == 0u && fresh[2] == 1u && fresh[3] == 0u && fresh[4] == 0u && fresh[5] == 1u,
              "changes: the third and sixth are new motifs, the rest one step each");
        check(mm.need >= 3u && mm.need <= 5u && mm.returns == 0u, "changes: after a change, 3-5 returns come before the next");
    }
    (void)i; (void)d0;
}

/* random motifs: the invariants hold for every draw, and the lengths and contours cover their ranges */
static void motif_tests(void)
{
    melody_t mm;
    uint32_t t, bad = 0u, len_seen = 0u, arch = 0u, falling = 0u;
    mm.rng = 99u;
    for (t = 0; t < 20000u; t++) {
        uint32_t i, up = 0u;
        melody_new_motif(&mm);
        if (!motif_ok(&mm.motif)) bad++;
        len_seen |= 1u << mm.motif.len;
        for (i = 1; i < mm.motif.len; i++) if (mm.motif.step[i] > mm.motif.step[i - 1]) up = 1u;
        if (up) arch++; else falling++;
    }
    check(bad == 0u, "new motifs: 20000 draws, each 3..6 notes, step 0 first, span <= 6, durations per the rules");
    check(len_seen == ((1u << 3) | (1u << 4) | (1u << 5) | (1u << 6)), "new motifs: every length 3, 4, 5 and 6 occurs");
    check(arch > 0u && falling > 0u, "new motifs: both falling and arched contours occur");
}

/* the full phrase cycle is deterministic: a run is the same twice, and a different seed gives a different run */
static void determinism_tests(void)
{
    static ev_t a[EV_MAX], b[EV_MAX], c[EV_MAX];
    uint32_t na, nb, nc, i, same_ab = 1u, differ_ac = 0u;
    uint32_t mb = 0u, mc = 0u;
    na = sim(a, SEEDS[1], PATHS[1], 2u, 3u, 0u, 900000u, &mb, &mc);
    nb = sim(b, SEEDS[1], PATHS[1], 2u, 3u, 0u, 900000u, &mb, &mc);
    nc = sim(c, SEEDS[2], PATHS[1], 2u, 3u, 0u, 900000u, &mb, &mc);
    if (na != nb) same_ab = 0u;
    for (i = 0; same_ab && i < na; i++)
        if (a[i].t != b[i].t || a[i].kind != b[i].kind || a[i].note != b[i].note || a[i].level != b[i].level) same_ab = 0u;
    if (na != nc) differ_ac = 1u;
    for (i = 0; !differ_ac && i < na; i++)
        if (a[i].t != c[i].t || a[i].note != c[i].note) differ_ac = 1u;
    check(na > 0u && same_ab, "determinism: the same seed gives the same events, notes and times");
    check(differ_ac, "determinism: a different melody seed gives different events");
}

/* the ms counter wraps: a run that starts 20 s before 0xFFFFFFFF gives the same events, at the same times from the start,
   as a run that starts at 0 */
static void wrap_tests(void)
{
    static ev_t a[EV_MAX], b[EV_MAX];
    uint32_t na, nb, i, same = 1u, mb = 0u, mc = 0u;
    na = sim(a, SEEDS[0], PATHS[0], 4u, 2u, 0u, WRAP_RUN_MS, &mb, &mc);
    nb = sim(b, SEEDS[0], PATHS[0], 4u, 2u, 0xFFFFFFFFu - 20000u, WRAP_RUN_MS, &mb, &mc);
    if (na != nb || na == 0u) same = 0u;
    for (i = 0; same && i < na; i++)
        if (a[i].t != b[i].t || a[i].kind != b[i].kind || a[i].note != b[i].note || a[i].level != b[i].level) same = 0u;
    check(same, "wrap: a run across fm1_ms = 0xFFFFFFFF gives the same events, at the same times, as the run without it");
}

/* melody_interrupt: an OFF comes next if a note was sounding, and then no note for at least 3000 ms. Both cases: a note
   sounding, and silence. The chord is fixed at degree 0, template ADD9 (fits every scale; checked with lit_fits). */
static void interrupt_tests(void)
{
    uint32_t sc, de, sd, ok_on = 1u, ok_off = 1u, cases = 0u, t;
    for (sd = 0; sd < 2u; sd++)
        for (sc = 0; sc < 6u; sc++)
            for (de = 0; de < 4u; de++) {
                uint32_t fixed = 8u, k;
                melody_t m;
                for (t = 0; t < 8u; t++) if (lit_fits(sc, 0, t)) { fixed = t; break; }   /* the first template that fits degree 0 */
                if (fixed == 8u) { check(0, "interrupt: a template fits degree 0 in every scale"); return; }
                melody_init(&m, SEEDS[sd], 0u);
                {   /* case A: stop on a note's ON, then interrupt at that ms */
                    uint32_t t_on = 0u, what = MELODY_NONE, note = 0u, level = 0u, first_after = 0u;
                    for (k = 1u; k < 200000u && what != MELODY_ON; k++) {
                        what = melody_tick(&m, k, sc, 0, fixed, 0, de, &note, &level);
                        t_on = k;
                    }
                    cases++;
                    if (what != MELODY_ON) { ok_on = 0u; continue; }
                    melody_interrupt(&m, t_on);
                    what = melody_tick(&m, t_on + 1u, sc, 0, fixed, 0, de, &note, &level);
                    if (what != MELODY_OFF) ok_on = 0u;
                    for (k = t_on + 2u; k < t_on + 20000u; k++) {
                        what = melody_tick(&m, k, sc, 0, fixed, 0, de, &note, &level);
                        if (what == MELODY_ON) { first_after = k; break; }
                    }
                    if (first_after < t_on + 3000u || first_after > t_on + 6000u) ok_on = 0u;
                }
                melody_init(&m, SEEDS[sd] ^ 0x77u, 0u);
                {   /* case B: stop on a note's OFF (silence), then interrupt: no OFF, and a rest of 3-6 s */
                    uint32_t t_off = 0u, what = MELODY_NONE, note = 0u, level = 0u, first_after = 0u, quiet_next;
                    for (k = 1u; k < 200000u && what != MELODY_OFF; k++) {
                        what = melody_tick(&m, k, sc, 0, fixed, 0, de, &note, &level);
                        t_off = k;
                    }
                    if (what != MELODY_OFF) { ok_off = 0u; continue; }
                    melody_interrupt(&m, t_off);
                    quiet_next = melody_tick(&m, t_off + 1u, sc, 0, fixed, 0, de, &note, &level);
                    if (quiet_next != MELODY_NONE) ok_off = 0u;
                    for (k = t_off + 2u; k < t_off + 20000u; k++) {
                        what = melody_tick(&m, k, sc, 0, fixed, 0, de, &note, &level);
                        if (what == MELODY_ON) { first_after = k; break; }
                    }
                    if (first_after < t_off + 3000u || first_after > t_off + 6000u) ok_off = 0u;
                }
            }
    check(cases == 2u * 6u * 4u && ok_on, "interrupt, sounding: the next tick is the OFF, then the next ON is 3-6 s later");
    check(ok_off, "interrupt, silent: no OFF, and the next ON is 3-6 s later");
}

/* the theme must come back changed: the cycle has to reach every transform, change its motif, and vary its tempo phrase
   by phrase. Reads the melody's own state at each phrase's first note, over a fixed chord (pentatonic, add9 on the root):
   the phrases' notes then depend only on the motif and the transform. Density 3, for the most phrases per hour. */
static void development_tests(void)
{
    static const uint32_t HOURS = 20u;
    uint32_t si, plain = 0u, frag = 0u, inv = 0u, aug = 0u, returns = 0u, statements = 0u;
    uint32_t mutated = 0u, fresh = 0u, gap_bad = 0u, unit_min = 0xFFFFFFFFu, unit_max = 0u;
    for (si = 0; si < 4u; si++) {
        static melody_t m;
        melody_motif_t last;
        uint32_t now, note, level, i, since = 0u, seen = 0u, changes = 0u;
        melody_init(&m, SEEDS[si], 0u);
        for (now = 20u; now < HOURS * 3600000u; now += 20u) {
            if (melody_tick(&m, now, SCALE_PENTATONIC, 0, CHORD_ADD9, 0, 3u, &note, &level) != MELODY_ON || m.idx != 0u) continue;
            if (seen) {
                uint32_t differ = last.len != m.motif.len, k;
                for (k = 0; k < m.motif.len && k < last.len; k++) differ += last.step[k] != m.motif.step[k];
                if (m.kind != MELODY_PH_ECHO && differ) {
                    if (last.len == m.motif.len && differ == 1u) mutated++;
                    else fresh++;
                }
            }
            if (m.mutations != changes) {           /* read from the counter: a change can leave the motif as it was (0.4%) */
                changes = m.mutations;
                if (since < 3u || since > 5u) gap_bad++;       /* a motif returns 3 to 5 times, then changes */
                since = 0u;
            }
            seen = 1u;
            last.len = m.motif.len;
            for (i = 0; i < m.motif.len; i++) last.step[i] = m.motif.step[i];
            if (m.kind == MELODY_PH_STATEMENT) {
                uint32_t per_unit = m.dur_ms[0] / m.motif.dur[0];
                statements++;
                if (per_unit < unit_min) unit_min = per_unit;
                if (per_unit > unit_max) unit_max = per_unit;
            } else if (m.kind == MELODY_PH_RETURN) {
                int32_t rise = (int32_t)m.note[1] - (int32_t)m.note[0];
                uint32_t per_unit = m.dur_ms[0] / m.motif.dur[0];
                returns++;
                since++;
                /* worked by hand: a first note at D3 is at most 450 * 1.15 * 1.15 * 1.06 = 631 ms per unit without AUGMENT,
                   and with it at least 450 * 0.85 * 1.5 * 1.15 * 0.94 = 620. So over 640 can only be AUGMENT */
                if (m.plen < m.motif.len) frag++;
                else if (per_unit > 640u) aug++;
                else if ((rise > 0) == (m.motif.step[1] < 0) && rise != 0) inv++;
                else plain++;
            }
        }
    }
    printf("melody: development: %u returns: %u plain, %u inverted, %u fragments, %u augmented; %u one-step mutations, %u new motifs; %u statements, first note %u..%u ms per unit\n",
           returns, plain, inv, frag, aug, mutated, fresh, statements, unit_min, unit_max);
    check(returns > 1000u, "development: a long run has over 1000 returns");
    check(inv * 8u >= returns && frag * 8u >= returns && plain * 8u >= returns, "development: inverted, fragment and plain returns each make at least 1 in 8");
    check(aug * 16u >= returns, "development: augmented returns (those clearly over the unaugmented limit) make at least 1 in 16");
    check(mutated > 50u && fresh > 25u, "development: the motif changes, both by one step and into a new motif");
    check(mutated >= fresh, "development: one-step changes outnumber new motifs (two of every three changes)");
    check(gap_bad == 0u, "development: a motif returns 3 to 5 times between changes");
    check(unit_max * 100u >= unit_min * 125u, "development: the tempo varies by at least 25% from phrase to phrase (jitter alone gives at most 13%)");
}

int main(void)
{
    matrix_tests();
    worked_examples();
    motif_tests();
    determinism_tests();
    wrap_tests();
    interrupt_tests();
    development_tests();
    printf("melody: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
