/* SPDX-License-Identifier: GPL-3.0-only */
/* The melody: a short theme on the top colour voice, which comes back changed, with long
 * silences between. Pure: no platform, no voice or envelope state. Time is passed in as now_ms, and every time comparison
 * is (int32_t)(now - t) >= 0, so the fm1_ms counter may wrap. melody_tick returns one note on or note off per call, and
 * engine.h turns it into an event for the top colour voice. Integers only, per docs/RULES.md.
 *
 * The cycle: a statement (the opening one, and one after each new motif), an echo of it, then a rest. A return of the
 * motif is one transform of it (none, inverted, a fragment, or augmented), re-anchored on the chord now sounding. After
 * 3 to 5 returns the motif changes by one step, and every third change is a new motif. Timing has a shape (rubato): each
 * phrase's unit and each note's length are drawn, so nothing sits on a grid. */
#pragma once
#include <stdint.h>
#include "planing.h"

#define MELODY_MOTIF_MAX 6u                     /* notes in a motif: 3..6 */
#define MELODY_SPAN_MAX 6                       /* degrees from a motif's lowest step to its highest. Six degrees is at most 15
                                                   semitones in every scale, so melody_place always fits a phrase into 60..86
                                                   by octaves. Every |step| is then at most 6, inside the 7 the motif allows */
#define MELODY_ROOT_NOTE 48u                    /* MIDI of the scale's root: engine.h's ROOT_BASE_NOTE (engine_test checks they match) */
#define MELODY_NOTE_MIN 60u                     /* the lowest melody note, MIDI */
#define MELODY_NOTE_MAX 86u                     /* the highest melody note, MIDI */
#define MELODY_ANCHOR_MIN 64u                   /* the anchor (the chord's top tone) is placed in MIDI 64..75: one note per pitch class */
#define MELODY_ANCHOR_MAX 75u
#define MELODY_FIT_TRIES 8u                     /* octave moves allowed to fit a phrase. One is always enough (see melody_place); the rest is a guard */
#define MELODY_MAKE_TRIES 64u                   /* motifs drawn before the fallback: the chance of all 64 failing is under 1e-26 */

#define MELODY_STATEMENT_LEVEL 200u             /* level (0..255) of a statement or a return: its amp is scaled by 200/256, -2.1 dB */
#define MELODY_ECHO_LEVEL 120u                  /* level of an echo: 120/256, -6.6 dB, so the echo is quieter than the statement */
#define MELODY_NOTE_MIN_MS 150u                 /* the shortest note, ms, after rubato: never shorter, so a note always sounds */
#define MELODY_OFF_PCT 80u                      /* the note is released at 80% of its length; the next note starts at 100% */
#define MELODY_FIRST_MIN_MS 6000u               /* the opening statement: 6-10 s after the engine starts */
#define MELODY_FIRST_SPAN_MS 4001u
#define MELODY_BREATH_MIN_MS 500u               /* the echo follows its phrase after a breath of 0.5-1.4 s */
#define MELODY_BREATH_SPAN_MS 901u
#define MELODY_INTERRUPT_MIN_MS 3000u           /* after a scale change or a resume: a rest of 3-6 s before the next phrase */
#define MELODY_INTERRUPT_SPAN_MS 3001u
#define MELODY_RETURN_MIN_TRIES 3u              /* returns of a motif before it changes: 3 to 5 */
#define MELODY_RETURN_SPAN_TRIES 3u
#define MELODY_FRAGMENT_LAST_UNITS 3u           /* a fragment's last note lasts 3 units, so it sounds as a cadence-free stop */
#define MELODY_RUBATO_MIN_PCT 85u               /* each phrase's unit is 85-115% of the density's unit */
#define MELODY_RUBATO_SPAN_PCT 31u
#define MELODY_NOTE_JITTER_MIN_PCT 94u          /* each note then gets its own 94-106% */
#define MELODY_NOTE_JITTER_SPAN_PCT 13u

/* the unit of a note's length, ms, before rubato, by density (ENC_ALGORITHM 0..3). A note is dur units long: a 4-unit
   note at D0 is 3.6 s, at D3 1.8 s. Denser settings are shorter, so the line moves faster. */
static const uint32_t MELODY_UNIT_MS[4] = { 900u, 750u, 600u, 450u };

/* the rest after a phrase, ms, inclusive, by density: the long silences that frame the theme */
static const struct { uint32_t min_ms, max_ms; } MELODY_REST_MS[4] = {
    { 14000u, 32000u },
    { 10000u, 24000u },
    { 7000u, 16000u },
    { 4000u, 10000u },
};

/* the four transforms of a return, and the three kinds of phrase */
enum { MELODY_XF_NONE = 0, MELODY_XF_INVERT = 1, MELODY_XF_FRAGMENT = 2, MELODY_XF_AUGMENT = 3, MELODY_XF_COUNT = 4 };
enum { MELODY_PH_STATEMENT = 0, MELODY_PH_RETURN = 1, MELODY_PH_ECHO = 2 };
enum { MELODY_IDLE = 0, MELODY_SOUND = 1 };

/* melody_tick's return values: a note on, a note off, or nothing this call */
#define MELODY_NONE 0u
#define MELODY_ON 1u
#define MELODY_OFF 2u

/* the motif: scale-degree offsets from its first note, each note's length in units, and the note count */
typedef struct {
    int8_t step[MELODY_MOTIF_MAX];              /* step[0] is always 0 */
    uint8_t dur[MELODY_MOTIF_MAX];              /* duration units, 1..4 */
    uint8_t len;                                /* 3..MELODY_MOTIF_MAX */
} melody_motif_t;

typedef struct {
    melody_motif_t motif;
    uint32_t rng;                               /* this melody's own xorshift32 state, never 0: its draws leave the chord's alone */
    uint32_t mode;                              /* MELODY_IDLE: waiting for next_ms. MELODY_SOUND: playing the phrase */
    uint32_t next_kind;                         /* IDLE: the kind of the phrase due at next_ms (MELODY_PH_*) */
    uint32_t kind;                              /* SOUND: the kind of the phrase playing */
    uint32_t played;                            /* 1 once the opening statement has started: an interrupt then returns, not restates */
    uint32_t on;                                /* SOUND: 1 while the current note sounds (its ON sent, its OFF not yet) */
    uint32_t off_due;                           /* 1: melody_interrupt cut a sounding note; the next tick sends its OFF */
    uint32_t idx, plen;                         /* the note playing, and the phrase's note count */
    uint32_t note[MELODY_MOTIF_MAX];            /* the phrase: MIDI notes */
    uint32_t dur_ms[MELODY_MOTIF_MAX];          /* the phrase: each note's length, ms (an echo replays these) */
    uint32_t level;                             /* the phrase's level: MELODY_STATEMENT_LEVEL or MELODY_ECHO_LEVEL */
    uint32_t t_note;                            /* SOUND: when the current note starts, ms. After the last note: when the phrase ends */
    uint32_t next_ms;                           /* IDLE: when the next phrase starts, ms */
    uint32_t returns, need, mutations;          /* returns of the motif since its last change; returns before the next change (3..5);
                                                   changes so far (every third is a new motif) */
} melody_t;

/* wrap-safe: true once fm1_ms has reached t */
static uint32_t melody_due(uint32_t now_ms, uint32_t t)
{
    return (int32_t)(now_ms - t) >= 0;
}

/* lo + a uniform draw over the inclusive span (span values in all) */
static uint32_t melody_range(uint32_t *rng, uint32_t lo, uint32_t span)
{
    return lo + rng_next(rng) % span;
}

/* the largest minus the smallest step of the first len notes */
static int32_t melody_span(const int8_t *step, uint32_t len)
{
    int32_t lo = 0, hi = 0;
    uint32_t i;
    for (i = 0; i < len; i++) {
        if (step[i] < lo) lo = step[i];
        if (step[i] > hi) hi = step[i];
    }
    return hi - lo;
}

/* a new motif: 3..6 notes; 2 in 3 falling, 1 in 3 an arch; each interval is
   one degree (3 in 4) or two (1 in 4). The arch rises for its first len/2 intervals and falls for the rest. Each note but
   the last lasts 1 unit (3 in 5) or 2; the last lasts 3 or 4. A draw whose span is over MELODY_SPAN_MAX is redrawn. */
static void melody_new_motif(melody_t *m)
{
    uint32_t tries, i, len, arch, p;
    for (tries = 0; tries < MELODY_MAKE_TRIES; tries++) {
        len = 3u + rng_next(&m->rng) % 4u;
        arch = rng_next(&m->rng) % 3u == 0u;     /* 1 in 3 is an arch */
        p = len / 2u;                           /* an arch's ascending intervals: 1..p */
        m->motif.step[0] = 0;
        for (i = 1; i < len; i++) {
            int32_t d = rng_next(&m->rng) % 4u == 0u ? 2 : 1;
            int32_t up = (arch && i <= p) ? d : -d;
            m->motif.step[i] = (int8_t)(m->motif.step[i - 1] + up);
        }
        for (i = 0; i + 1 < len; i++)
            m->motif.dur[i] = rng_next(&m->rng) % 5u < 2u ? 2u : 1u;     /* 2 in 5 are 2 units, 3 in 5 are 1 */
        m->motif.dur[len - 1u] = 3u + rng_next(&m->rng) % 2u;           /* the last note: 3 or 4 units */
        m->motif.len = (uint8_t)len;
        if (melody_span(m->motif.step, len) <= MELODY_SPAN_MAX) return;
    }
    /* fallback, never reached in practice: a short falling motif, span 2 */
    m->motif.len = 3u;
    m->motif.step[0] = 0; m->motif.step[1] = -1; m->motif.step[2] = -2;
    m->motif.dur[0] = 1u; m->motif.dur[1] = 1u; m->motif.dur[2] = 3u;
}

/* one interior step (index 1..len-2, never step[0]) moves by one degree, the way that stays within the span. Changes
   nothing when neither direction fits. */
static void melody_mutate_step(melody_t *m)
{
    uint32_t len = m->motif.len, i, k;
    int32_t d = (rng_next(&m->rng) & 1u) ? 1 : -1;
    i = 1u + rng_next(&m->rng) % (len - 2u);
    for (k = 0; k < 2u; k++, d = -d) {
        int8_t old = m->motif.step[i];
        m->motif.step[i] = (int8_t)(old + d);
        if (melody_span(m->motif.step, len) <= MELODY_SPAN_MAX) return;
        m->motif.step[i] = old;
    }
}

/* after MELODY_RETURN_MIN_TRIES..+2 returns: the motif changes. Every third change is a new motif (returns 1, so the
   next phrase is introduced as a statement); the others change one step (returns 0). */
static uint32_t melody_change_motif(melody_t *m)
{
    uint32_t fresh = m->mutations % 3u == 2u;
    if (fresh) melody_new_motif(m);
    else melody_mutate_step(m);
    m->mutations++;
    m->returns = 0u;
    m->need = melody_range(&m->rng, MELODY_RETURN_MIN_TRIES, MELODY_RETURN_SPAN_TRIES);
    return fresh;
}

/* the transform of a motif, as steps and units for its k notes (k is returned). The steps and units are written to st
   and units, one entry per note. NONE copies the motif. INVERT negates every step. FRAGMENT keeps the first keep notes
   (2..len-1) and gives the last 3 units. AUGMENT is not here: the unit is scaled in melody_time_notes. */
static uint32_t melody_transform(const melody_motif_t *mo, uint32_t xf, uint32_t keep, int32_t *st, uint32_t *units)
{
    uint32_t i, k = mo->len;
    for (i = 0; i < mo->len; i++) {
        st[i] = mo->step[i];
        units[i] = mo->dur[i];
    }
    if (xf == MELODY_XF_INVERT) {
        for (i = 0; i < mo->len; i++) st[i] = -st[i];
    } else if (xf == MELODY_XF_FRAGMENT) {
        if (keep < 2u) keep = 2u;
        if (keep > mo->len - 1u) keep = mo->len - 1u;
        k = keep;
        units[k - 1u] = MELODY_FRAGMENT_LAST_UNITS;
    }
    return k;
}

/* the scale degree of the anchor: the one whose note (MELODY_ROOT_NOTE + its semitone) lies in MELODY_ANCHOR_MIN..MAX and
   has pitch class pc. Returns 0 when pc is not in the scale. */
static uint32_t melody_anchor(uint32_t scale, int32_t pc, int32_t *degree)
{
    int32_t d, n = (int32_t)SCALE_TABLE[scale].n;
    for (d = 0; d <= 3 * n; d++) {
        int32_t midi = (int32_t)MELODY_ROOT_NOTE + scale_degree_to_semitone(scale, d);
        if (midi >= (int32_t)MELODY_ANCHOR_MIN && midi <= (int32_t)MELODY_ANCHOR_MAX && pitch_class(midi) == pc) {
            *degree = d;
            return 1u;
        }
    }
    return 0u;
}

/* the phrase's MIDI notes: the anchor degree d0 plus each step. An octave is SCALE_TABLE[scale].n degrees, so the whole
   phrase moves an octave at a time until its highest note is at most MELODY_NOTE_MAX and its lowest at least
   MELODY_NOTE_MIN. A phrase spans at most 15 semitones (see MELODY_SPAN_MAX) and its anchor is in 64..75, so a single
   octave move from the anchor always puts every note in 60..86: the loop takes at most one move. Then the last note
   leaves the tonic (pitch class 0) and the drone's pitch
   class: it moves up a scale degree at a time to the first note that is neither, or down if that goes over
   MELODY_NOTE_MAX. Returns 0 if no fit exists (not reached with a span of at most 15 semitones). */
static uint32_t melody_place(uint32_t scale, int32_t d0, const int32_t *st, uint32_t k, int32_t drone_pc, uint32_t *note)
{
    int32_t n = (int32_t)SCALE_TABLE[scale].n, lo, hi, midi, dl, dd, i;
    uint32_t tries, last = k - 1u, found;
    for (tries = 0; ; tries++) {
        lo = 1000;
        hi = -1000;
        for (i = 0; i < (int32_t)k; i++) {
            midi = (int32_t)MELODY_ROOT_NOTE + scale_degree_to_semitone(scale, d0 + st[i]);
            note[i] = (uint32_t)midi;
            if (midi < lo) lo = midi;
            if (midi > hi) hi = midi;
        }
        if (lo >= (int32_t)MELODY_NOTE_MIN && hi <= (int32_t)MELODY_NOTE_MAX) break;
        if (tries == MELODY_FIT_TRIES) return 0u;
        if (hi > (int32_t)MELODY_NOTE_MAX) d0 -= n;
        else d0 += n;
    }
    midi = (int32_t)note[last];
    if (pitch_class(midi) == 0 || pitch_class(midi) == drone_pc) {
        found = 0u;
        dl = d0 + st[last];
        for (dd = dl + 1; !found; dd++) {       /* up: the first degree that is neither, if it is in range */
            midi = (int32_t)MELODY_ROOT_NOTE + scale_degree_to_semitone(scale, dd);
            if (midi > (int32_t)MELODY_NOTE_MAX) break;
            if (pitch_class(midi) != 0 && pitch_class(midi) != drone_pc) { found = 1u; note[last] = (uint32_t)midi; }
        }
        for (dd = dl - 1; !found; dd--) {       /* down, when up went over the top */
            midi = (int32_t)MELODY_ROOT_NOTE + scale_degree_to_semitone(scale, dd);
            if (midi < (int32_t)MELODY_NOTE_MIN) return 0u;
            if (pitch_class(midi) != 0 && pitch_class(midi) != drone_pc) { found = 1u; note[last] = (uint32_t)midi; }
        }
    }
    return 1u;
}

/* each note's length in ms: units x the phrase's unit (density's table, times 85-115%, times 3/2 for AUGMENT) x a
   position percent (the first note 115, the last 130, the second-to-last 110, the others 90: the phrase pushes through
   its middle and relaxes at the end) x the note's own 94-106%. Never under MELODY_NOTE_MIN_MS. */
static void melody_time_notes(melody_t *m, uint32_t k, const uint32_t *units, uint32_t density, uint32_t augment)
{
    uint32_t base = MELODY_UNIT_MS[density] * melody_range(&m->rng, MELODY_RUBATO_MIN_PCT, MELODY_RUBATO_SPAN_PCT) / 100u;
    uint32_t i, pos, pct, ms;
    if (augment) base = base * 3u / 2u;
    for (i = 0; i < k; i++) {
        pos = i == 0u ? 115u : i + 1u == k ? 130u : i + 2u == k ? 110u : 90u;
        pct = pos * melody_range(&m->rng, MELODY_NOTE_JITTER_MIN_PCT, MELODY_NOTE_JITTER_SPAN_PCT) / 100u;
        ms = units[i] * base * pct / 100u;
        m->dur_ms[i] = ms < MELODY_NOTE_MIN_MS ? MELODY_NOTE_MIN_MS : ms;
    }
}

/* builds the phrase for this transform: its notes and lengths, anchored on the chord's top tone. Returns 0 when there is
   no anchor or no fit; the phrase is then not replaced. */
static uint32_t melody_build(melody_t *m, uint32_t scale, int32_t chord_degree, uint32_t chord_templ, int32_t drone_st,
                             uint32_t density, uint32_t xf, uint32_t keep)
{
    int32_t st[MELODY_MOTIF_MAX], d0 = 0, root_st, top_pc;
    uint32_t units[MELODY_MOTIF_MAX], note[MELODY_MOTIF_MAX], k, i;
    root_st = scale_degree_to_semitone(scale, chord_degree);
    top_pc = pitch_class(root_st + chord_voice_semitone(chord_templ, 2u));   /* the one tone no chord voice plays */
    if (!melody_anchor(scale, top_pc, &d0)) return 0u;
    k = melody_transform(&m->motif, xf, keep, st, units);
    if (!melody_place(scale, d0, st, k, pitch_class(drone_st), note)) return 0u;
    melody_time_notes(m, k, units, density, xf == MELODY_XF_AUGMENT);
    for (i = 0; i < k; i++) m->note[i] = note[i];
    m->plen = k;
    return 1u;
}

/* starts the phrase that is due at next_ms: an echo replays the stored phrase; a statement or a return is built. A return
   may first change the motif. Returns 0 when the phrase cannot be built. */
static uint32_t melody_start(melody_t *m, uint32_t scale, int32_t chord_degree, uint32_t chord_templ, int32_t drone_st,
                             uint32_t density)
{
    uint32_t kind = m->next_kind, xf = MELODY_XF_NONE, keep = 0u;
    if (kind == MELODY_PH_ECHO) {
        m->level = MELODY_ECHO_LEVEL;
    } else {
        if (kind == MELODY_PH_RETURN && m->returns >= m->need && melody_change_motif(m)) kind = MELODY_PH_STATEMENT;
        if (kind == MELODY_PH_RETURN) {
            xf = rng_next(&m->rng) % MELODY_XF_COUNT;
            if (xf == MELODY_XF_FRAGMENT) keep = 2u + rng_next(&m->rng) % (m->motif.len - 2u);   /* 2..len-1 notes */
        }
        if (!melody_build(m, scale, chord_degree, chord_templ, drone_st, density, xf, keep)) return 0u;
        m->level = MELODY_STATEMENT_LEVEL;
        if (kind == MELODY_PH_STATEMENT) m->played = 1u;
        else m->returns++;
    }
    m->kind = kind;
    m->mode = MELODY_SOUND;
    m->idx = 0u;
    m->on = 0u;
    m->t_note = m->next_ms;
    return 1u;
}

/* the phrase has ended at end_ms (its last note's full length). A statement is echoed; a return is echoed with a 1 in 2
   chance, else rested; an echo is rested. A rest is a density's MELODY_REST_MS draw, then a return. */
static void melody_end(melody_t *m, uint32_t end_ms, uint32_t density)
{
    m->mode = MELODY_IDLE;
    if (m->kind == MELODY_PH_STATEMENT || (m->kind == MELODY_PH_RETURN && rng_next(&m->rng) % 2u == 0u)) {
        m->next_kind = MELODY_PH_ECHO;
        m->next_ms = end_ms + melody_range(&m->rng, MELODY_BREATH_MIN_MS, MELODY_BREATH_SPAN_MS);
    } else {
        m->next_kind = MELODY_PH_RETURN;
        m->next_ms = end_ms + melody_range(&m->rng, MELODY_REST_MS[density].min_ms,
                                           MELODY_REST_MS[density].max_ms - MELODY_REST_MS[density].min_ms + 1u);
    }
}

/* starts the melody: the first statement 6-10 s from now, then its own cycle. seed is any value: 0 is mapped to 1. */
static void melody_init(melody_t *m, uint32_t seed, uint32_t now_ms)
{
    m->rng = seed ? seed : 1u;
    melody_new_motif(m);
    m->need = melody_range(&m->rng, MELODY_RETURN_MIN_TRIES, MELODY_RETURN_SPAN_TRIES);
    m->mode = MELODY_IDLE;
    m->next_kind = MELODY_PH_STATEMENT;
    m->kind = MELODY_PH_STATEMENT;
    m->played = 0u;
    m->on = 0u;
    m->off_due = 0u;
    m->idx = 0u;
    m->plen = 0u;
    m->level = 0u;
    m->t_note = now_ms;
    m->returns = 0u;
    m->mutations = 0u;
    m->next_ms = now_ms + melody_range(&m->rng, MELODY_FIRST_MIN_MS, MELODY_FIRST_SPAN_MS);
}

/* a scale change or a resume: a sounding note is released at the next tick, the phrase is dropped, and after a rest of
   3-6 s a return comes back (the opening statement, if it has not yet been played). Silence first, whatever was due. */
static void melody_interrupt(melody_t *m, uint32_t now_ms)
{
    if (m->mode == MELODY_SOUND && m->on) m->off_due = 1u;
    m->mode = MELODY_IDLE;
    m->next_kind = m->played ? MELODY_PH_RETURN : MELODY_PH_STATEMENT;
    m->next_ms = now_ms + melody_range(&m->rng, MELODY_INTERRUPT_MIN_MS, MELODY_INTERRUPT_SPAN_MS);
}

/* one main-loop step. Returns MELODY_ON or MELODY_OFF (at most one of them), or MELODY_NONE, and writes the MIDI note
   (and, for ON, its level) through the pointers. chord_degree and chord_templ are the chord now sounding; drone_st is the
   drone's semitone above the scale's root; density is 0..3. */
static uint32_t melody_tick(melody_t *m, uint32_t now_ms, uint32_t scale, int32_t chord_degree, uint32_t chord_templ,
                            int32_t drone_st, uint32_t density, uint32_t *note, uint32_t *level)
{
    if (m->off_due) {
        m->off_due = 0u;
        m->on = 0u;
        *note = m->note[m->idx];
        *level = m->level;
        return MELODY_OFF;
    }
    if (m->mode == MELODY_IDLE) {
        if (!melody_due(now_ms, m->next_ms)) return MELODY_NONE;
        if (!melody_start(m, scale, chord_degree, chord_templ, drone_st, density)) {
            m->next_kind = MELODY_PH_RETURN;     /* no phrase this time: rest, then try a return */
            m->next_ms = now_ms + melody_range(&m->rng, MELODY_REST_MS[density].min_ms,
                                               MELODY_REST_MS[density].max_ms - MELODY_REST_MS[density].min_ms + 1u);
            return MELODY_NONE;
        }
    }
    if (!m->on) {
        if (!melody_due(now_ms, m->t_note)) return MELODY_NONE;
        m->on = 1u;
        *note = m->note[m->idx];
        *level = m->level;
        return MELODY_ON;
    }
    if (!melody_due(now_ms, m->t_note + m->dur_ms[m->idx] * MELODY_OFF_PCT / 100u)) return MELODY_NONE;
    *note = m->note[m->idx];
    *level = m->level;
    m->on = 0u;
    m->t_note += m->dur_ms[m->idx];          /* the next note starts at 100% of this one's length */
    m->idx++;
    if (m->idx == m->plen) melody_end(m, m->t_note, density);
    return MELODY_OFF;
}
