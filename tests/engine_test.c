/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine (firmware/src/engine.h) and the saved settings (firmware/src/settings.h), run on a fake clock:
 * 50 ms steps over simulated hours, several seeds, all six scales, all densities and drifts. The sound is modelled
 * simply and independently of envelope.h: a colour voice sounds from its note on to its note off, and is quiet
 * QUIET_AFTER_OFF_MS after that. The scales are literal sets here, not the code's tables. No platform dependency:
 * $CC -o build/host/engine_test tests/engine_test.c && build/host/engine_test */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "../firmware/src/engine.h"

static int fails;
static void check(int ok, const char *what) { printf("engine: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

#define TICK_MS 50u                             /* the fake clock's step, about the main loop's rate */
#define QUIET_AFTER_OFF_MS 6000u                /* the model: a voice is quiet 6 s after its note off (envelope_test.c: under 6 s) */
#define SIM_MS 10800000u                        /* 3 simulated hours per run */
#define SIM_TICKS (SIM_MS / TICK_MS)            /* 216000 ticks */
#define LOG_MAX 8192u                           /* events kept per run when logging is on */
#define RATE_MARGIN_PCT 10                      /* a denser setting must make at least this much more notes than the one below */

/* literal scale sets, as pitch classes above C (the tonic every root sits on), in planing.h's order */
static const int WHOLETONE_PC[] = { 0, 2, 4, 6, 8, 10 };
static const int PENTATONIC_PC[] = { 0, 2, 4, 7, 9 };
static const int MINOR_PENT_PC[] = { 0, 3, 5, 7, 10 };
static const int LYDIAN_PC[] = { 0, 2, 4, 6, 7, 9, 11 };
static const int MIXOLYDIAN_PC[] = { 0, 2, 4, 5, 7, 9, 10 };
static const int DORIAN_PC[] = { 0, 2, 3, 5, 7, 9, 10 };
static const int *const SCALE_PC[6] = { WHOLETONE_PC, PENTATONIC_PC, MINOR_PENT_PC, LYDIAN_PC, MIXOLYDIAN_PC, DORIAN_PC };
static const uint32_t SCALE_N[6] = { 6u, 5u, 5u, 7u, 7u, 7u };

static int in_scale(uint32_t scale, uint32_t note)
{
    uint32_t i, pc = note % 12u;
    for (i = 0; i < SCALE_N[scale]; i++)
        if ((uint32_t)SCALE_PC[scale][i] == pc) return 1;
    return 0;
}

/* the chord templates as literal semitones above the root, in planing.h's order (not the code's table) */
static const int CHORD_ST[8][3] = { { 4, 7, 11 }, { 4, 7, 14 }, { 2, 7, 12 }, { 5, 7, 12 },
                                    { 4, 9, 14 }, { 5, 10, 15 }, { 4, 10, 14 }, { 4, 8, 14 } };

/* how many of the new chord's three notes the old chord also has, by pitch class. Roots are semitones from C, any sign */
static uint32_t lit_shared(int32_t root_old, uint32_t t_old, int32_t root_new, uint32_t t_new)
{
    uint32_t a, b, n = 0u;
    for (b = 0; b < 3u; b++) {
        int32_t pn = ((root_new + CHORD_ST[t_new][b]) % 12 + 12) % 12;
        for (a = 0; a < 3u; a++)
            if (((root_old + CHORD_ST[t_old][a]) % 12 + 12) % 12 == pn) { n++; break; }
    }
    return n;
}

typedef struct { uint32_t t, kind, voice, note; } rec_t;   /* t: ms since the run started */

typedef struct {
    uint32_t notes, drones, bad_scale, bad_register, bad_drone, bad_quiet, double_on, orphan_off;
    uint32_t paused_events, held_at_pause, max_ons, triple_ticks, too_many, log_full;
    uint32_t moves, shared_sum;                 /* chord moves (a re-plane or re-root), and the notes each kept from the chord before */
    uint32_t top_on;                            /* chord voices (0, 1) that sounded the chord's top tone */
} stats_t;

typedef struct {
    uint32_t sounding[ENGINE_VOICES];           /* 1 from a voice's note on to its note off */
    uint32_t quiet_at[ENGINE_VOICES];           /* when a released voice counts as quiet again */
} env_model_t;

typedef struct {
    engine_t e;
    env_model_t env;
    stats_t st;
    rec_t log[LOG_MAX];
    uint32_t log_n, start, scale, log_on;
    uint32_t top_pc;                            /* the pitch class of the chord's top tone now (CHORD_ST[templ][2] above the root) */
} sim_t;

/* the chord's top tone's pitch class now, from the engine's root and template */
static void sim_set_top(sim_t *s, uint32_t scale)
{
    int32_t top_st = scale_degree_to_semitone(scale, s->e.home_degree + s->e.plane_degree) + CHORD_ST[s->e.chord_templ][2];
    s->top_pc = (uint32_t)(((top_st % 12) + 12) % 12);
}

/* a voice is quiet when it is not sounding and its release has run out (wrap-safe) */
static uint32_t model_quiet(const env_model_t *m, uint32_t now)
{
    uint32_t v, mask = 0u;
    for (v = 0; v < ENGINE_VOICES; v++)
        if (!m->sounding[v] && (int32_t)(now - m->quiet_at[v]) >= 0) mask |= 1u << v;
    return mask;
}

/* checks one call's events against the rules and the envelope model, and logs them if asked */
static void sim_apply(sim_t *s, const engine_event_t *ev, uint32_t n, uint32_t now, uint32_t quiet)
{
    uint32_t i, v, note, ons = 0u;
    if (n > ENGINE_MAX_EVENTS) s->st.too_many++;
    for (i = 0; i < n && i < ENGINE_MAX_EVENTS; i++) {
        v = ev[i].voice;
        note = ev[i].note;
        if (s->log_on) {
            if (s->log_n < LOG_MAX) {
                s->log[s->log_n].t = now - s->start;
                s->log[s->log_n].kind = ev[i].kind;
                s->log[s->log_n].voice = v;
                s->log[s->log_n].note = note;
                s->log_n++;
            } else {
                s->st.log_full++;
            }
        }
        if (ev[i].kind == EV_DRONE) {
            s->st.drones++;
            if (note < 24u || note > 48u) s->st.bad_drone++;
            if (!in_scale(s->scale, note)) s->st.bad_scale++;
        } else if (ev[i].kind == EV_NOTE_ON) {
            if (v >= ENGINE_VOICES || s->env.sounding[v]) s->st.double_on++;
            if (v < ENGINE_CHORD_VOICES && !((quiet >> v) & 1u)) s->st.bad_quiet++;   /* the melody is not held to quiet */
            if (v < ENGINE_CHORD_VOICES) {
                if (note < 36u || note > 82u) s->st.bad_register++;                    /* the chord's two lower tones */
                if (note % 12u == s->top_pc) s->st.top_on++;                           /* ...never the chord's top tone */
            } else if (note < 60u || note > 86u) {
                s->st.bad_register++;                                                  /* the melody: 60..86 (melody.h) */
            }
            if (!in_scale(s->scale, note)) s->st.bad_scale++;
            if (v < ENGINE_VOICES) s->env.sounding[v] = 1u;
            s->st.notes++;
            ons++;
        } else {                                /* EV_NOTE_OFF */
            if (v >= ENGINE_VOICES || !s->env.sounding[v]) s->st.orphan_off++;
            if (v < ENGINE_VOICES) {
                s->env.sounding[v] = 0u;
                s->env.quiet_at[v] = now + QUIET_AFTER_OFF_MS;
            }
        }
    }
    if (ons > s->st.max_ons) s->st.max_ons = ons;
    if (ons == ENGINE_VOICES) s->st.triple_ticks++;
}

typedef struct {
    uint32_t seed, scale, density, drift, lead, start, ticks;
    uint32_t pause_k, resume_k;                 /* 0: none. Pause before tick pause_k; resume in place of tick resume_k */
    uint32_t mute_k;                            /* 0: none. Before tick mute_k no voice counts as quiet */
    uint32_t log_on;
} run_t;

/* one run on the fake clock: engine_init at c->start, then c->ticks steps of TICK_MS. Pausing clears the envelopes,
   as impressionism.c does, so the model's voices go quiet at the pause. */
static void sim_run(sim_t *s, const run_t *c)
{
    engine_event_t ev[ENGINE_MAX_EVENTS];
    settings_t set;
    uint32_t k, now, n, quiet, v, t_before, plane_before, root_before;
    int32_t root_st_before;
    int in_pause;

    memset(s, 0, sizeof *s);
    settings_set_defaults(&set);
    set.scale = (uint8_t)c->scale;
    set.density = (uint8_t)c->density;
    set.drift = (uint8_t)c->drift;
    set.voice_lead = (uint8_t)c->lead;
    s->start = c->start;
    s->scale = c->scale;
    s->log_on = c->log_on;
    for (v = 0; v < ENGINE_VOICES; v++) s->env.quiet_at[v] = c->start;
    n = engine_init(&s->e, c->seed, &set, c->start, ev);
    sim_set_top(s, c->scale);
    sim_apply(s, ev, n, c->start, 0u);
    for (k = 1; k <= c->ticks; k++) {
        now = c->start + k * TICK_MS;
        in_pause = c->pause_k && k >= c->pause_k && (!c->resume_k || k < c->resume_k);
        quiet = (c->mute_k && k < c->mute_k) ? 0u : model_quiet(&s->env, now);
        if (c->pause_k && k == c->pause_k) {
            for (v = 0; v < ENGINE_VOICES; v++) {
                if (s->env.sounding[v]) s->st.held_at_pause++;
                if (v == ENGINE_MELODY_VOICE) continue;   /* the melody's note is still held: its OFF is sent at the first tick after resume */
                s->env.sounding[v] = 0u;
                s->env.quiet_at[v] = now;
            }
            engine_pause(&s->e, now);
        }
        t_before = s->e.chord_templ;
        plane_before = s->e.next_plane_ms;
        root_before = s->e.next_root_ms;
        root_st_before = scale_degree_to_semitone(c->scale, s->e.home_degree + s->e.plane_degree);
        if (c->resume_k && k == c->resume_k) n = engine_resume(&s->e, now, &set, ev);
        else n = engine_tick(&s->e, now, &set, quiet, ev);
        if (!(c->resume_k && k == c->resume_k) && (s->e.next_plane_ms != plane_before || s->e.next_root_ms != root_before)) {
            s->st.moves++;                      /* the chord moved: count the notes it kept (a move can land on the same chord) */
            s->st.shared_sum += lit_shared(root_st_before, t_before, scale_degree_to_semitone(c->scale, s->e.home_degree + s->e.plane_degree), s->e.chord_templ);
        }
        if (in_pause) s->st.paused_events += n;
        sim_set_top(s, c->scale);
        sim_apply(s, ev, n, now, quiet);
    }
}

/* the settings, one field at a time (no initializer) */
static void settings_of(settings_t *s, uint32_t scale, uint32_t density, uint32_t drift, uint32_t color, uint32_t drone_on)
{
    s->scale = (uint8_t)scale;
    s->density = (uint8_t)density;
    s->drift = (uint8_t)drift;
    s->color = (uint8_t)color;
    s->drone_on = (uint8_t)drone_on;
    s->voice_lead = 0u;
    s->mood = 0u;
}

static void settings_tests(void)
{
    settings_t d, b, m;
    uint32_t base, w, all_differ = 1u;

    check(sizeof(settings_t) == 7u, "settings: seven one-byte fields, a 7-byte layout (a block saved at 5 bytes reads as nothing saved)");
    check(offsetof(settings_t, scale) == 0u && offsetof(settings_t, density) == 1u && offsetof(settings_t, drift) == 2u &&
          offsetof(settings_t, color) == 3u && offsetof(settings_t, drone_on) == 4u &&
          offsetof(settings_t, voice_lead) == 5u && offsetof(settings_t, mood) == 6u,
          "settings: the first five fields keep their places, voice_lead is byte 5 and mood byte 6");
    settings_set_defaults(&d);
    check(d.scale == 1u && d.density == 0u && d.drift == 0u && d.color == 12u && d.drone_on == 1u,
          "settings: defaults are pentatonic, density 0, drift 0, colour 12, drone on");
    check(d.voice_lead == 0u && d.mood == 0u, "settings: voice leading defaults to 0 (the uniform pick) and mood to 0");
    check(settings_valid(&d), "settings: the defaults are valid");

    settings_of(&b, 5u, 3u, 3u, 31u, 1u);
    check(settings_valid(&b), "settings: the top of every range (scale 5, density 3, drift 3, colour 31, drone 1) is valid");
    settings_of(&b, 6u, 0u, 0u, 0u, 0u);
    check(!settings_valid(&b), "settings: scale 6 (past the last scale) is rejected");
    settings_of(&b, 0u, 4u, 0u, 0u, 0u);
    check(!settings_valid(&b), "settings: density 4 is rejected");
    settings_of(&b, 0u, 0u, 4u, 0u, 0u);
    check(!settings_valid(&b), "settings: drift 4 is rejected");
    settings_of(&b, 0u, 0u, 0u, 32u, 0u);
    check(!settings_valid(&b), "settings: colour 32 is rejected");
    settings_of(&b, 0u, 0u, 0u, 0u, 2u);
    check(!settings_valid(&b), "settings: drone_on 2 is rejected");
    settings_of(&b, 0u, 0u, 0u, 0u, 255u);
    check(!settings_valid(&b), "settings: a stored byte of 255 in the drone field is rejected");
    settings_of(&b, 5u, 3u, 3u, 31u, 1u);
    b.voice_lead = 3u; b.mood = 7u;
    check(settings_valid(&b), "settings: voice_lead 3 and mood 7, the tops of their ranges, are valid");
    b.voice_lead = 4u; b.mood = 0u;
    check(!settings_valid(&b), "settings: voice_lead 4 is rejected");
    b.voice_lead = 0u; b.mood = 8u;
    check(!settings_valid(&b), "settings: mood 8 is rejected");
    b.voice_lead = 255u; b.mood = 0u;
    check(!settings_valid(&b), "settings: a stored byte of 255 in the voice_lead field is rejected");

    check(knob_apply(0u, -1, 3u) == 0u, "knob: from 0, one detent down stays at 0 (bottom clamp)");
    check(knob_apply(0u, -100, 31u) == 0u, "knob: a large turn down clamps at 0");
    check(knob_apply(3u, 1, 3u) == 3u, "knob: from the top, one detent up stays at the max (top clamp)");
    check(knob_apply(5u, 100, 31u) == 31u, "knob: a large turn up clamps at the colour max, 31");
    check(knob_apply(1u, 2, 3u) == 3u, "knob: two detents up from 1 land on 3");
    check(knob_apply(12u, 0, 31u) == 12u, "knob: no turn leaves the value alone");

    /* the state word: default settings at chord 0 are drone bit 0x10, colour 12 (0x180), scale 1 (0x2000) */
    check(settings_state_word(&d, 0u) == 0x2190u, "state word: the defaults at chord 0 are 0x2190 (scale 1, colour 12, drone on)");
    settings_of(&m, 5u, 3u, 3u, 31u, 1u);
    check(settings_state_word(&m, 7u) == 0xBFFFu, "state word: the first five fields at their maximum (chord 7) pack into 0xBFFF, no overlap");
    m.voice_lead = 3u; m.mood = 7u;
    check(settings_state_word(&m, 7u) == 0x1FBFFFu, "state word: every field at its maximum packs into 0x1FBFFF, no overlap");
    settings_of(&m, 0u, 0u, 0u, 0u, 0u);
    m.voice_lead = 3u;
    check(settings_state_word(&m, 0u) == 0x30000u, "state word: voice_lead 3 alone sits in bits 16-17");
    settings_of(&m, 0u, 0u, 0u, 0u, 0u);
    m.mood = 7u;
    check(settings_state_word(&m, 0u) == 0x1C0000u, "state word: mood 7 alone sits in bits 18-20");
    settings_of(&m, 0u, 3u, 0u, 0u, 0u);
    check(settings_state_word(&m, 0u) == 0x3u, "state word: density 3 alone sits in bits 0-1");
    settings_of(&m, 0u, 0u, 3u, 0u, 0u);
    check(settings_state_word(&m, 0u) == 0xCu, "state word: drift 3 alone sits in bits 2-3");
    settings_of(&m, 0u, 0u, 0u, 0u, 1u);
    check(settings_state_word(&m, 0u) == 0x10u, "state word: the drone bit alone sits in bit 4");
    settings_of(&m, 0u, 0u, 0u, 31u, 0u);
    check(settings_state_word(&m, 0u) == 0x3E0u, "state word: colour 31 alone sits in bits 5-9");
    settings_of(&m, 0u, 0u, 0u, 0u, 0u);
    check(settings_state_word(&m, 7u) == 0x1C00u, "state word: chord template 7 alone sits in bits 10-12");
    settings_of(&m, 5u, 0u, 0u, 0u, 0u);
    check(settings_state_word(&m, 0u) == 0xA000u, "state word: scale 5 alone sits in bits 13-15");

    /* the word must change when the chord, or any one setting, changes */
    base = settings_state_word(&d, 0u);
    settings_of(&b, 2u, 0u, 0u, 12u, 1u);  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 1u, 0u, 12u, 1u);  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 0u, 1u, 12u, 1u);  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 0u, 0u, 13u, 1u);  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 0u, 0u, 12u, 0u);  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 0u, 0u, 12u, 1u);  b.voice_lead = 1u;  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    settings_of(&b, 1u, 0u, 0u, 12u, 1u);  b.mood = 1u;  if (settings_state_word(&b, 0u) == base) all_differ = 0;
    w = settings_state_word(&d, 1u);
    if (w == base) all_differ = 0;
    check(all_differ, "state word: a step in any one setting (voice_lead and mood included), or a new chord, changes the word");
}

int main(void)
{
    static sim_t s;                              /* static: the logs are large */
    static sim_t a, bb;
    static const uint32_t SEEDS[4] = { 1u, 0x9E3779B9u, 0xDEADBEEFu, 0x2545F491u };
    uint32_t si, sc, dr, de, runs = 0u, d, minutes_per_density;
    uint64_t notes_by_density[4] = { 0u, 0u, 0u, 0u };
    uint32_t bad_scale = 0, bad_register = 0, bad_drone = 0, bad_quiet = 0, double_on = 0, orphan_off = 0;
    uint32_t drones = 0, max_ons = 0, too_many = 0, top_on = 0;

    settings_tests();
    check(ROOT_BASE_NOTE == MELODY_ROOT_NOTE, "melody: melody.h's root note is engine.h's ROOT_BASE_NOTE (the same MIDI register)");

    /* the matrix: every seed x scale x drift x density, 3 simulated hours each */
    for (si = 0; si < 4u; si++)
        for (sc = 0; sc < 6u; sc++)
            for (dr = 0; dr < 4u; dr++)
                for (de = 0; de < 4u; de++) {
                    run_t c;
                    c.seed = SEEDS[si]; c.scale = sc; c.density = de; c.drift = dr;
                    c.start = 0u; c.ticks = SIM_TICKS; c.pause_k = 0u; c.resume_k = 0u; c.mute_k = 0u; c.log_on = 0u; c.lead = 0u;
                    sim_run(&s, &c);
                    runs++;
                    notes_by_density[de] += s.st.notes;
                    bad_scale += s.st.bad_scale;
                    bad_register += s.st.bad_register;
                    bad_drone += s.st.bad_drone;
                    bad_quiet += s.st.bad_quiet;
                    double_on += s.st.double_on;
                    orphan_off += s.st.orphan_off;
                    drones += s.st.drones;
                    too_many += s.st.too_many;
                    top_on += s.st.top_on;
                    if (s.st.max_ons > max_ons) max_ons = s.st.max_ons;
                }
    check(bad_scale == 0u, "every note-on and drone note is in its scale (4 seeds x 6 scales x 4 drifts x 4 densities)");
    check(bad_drone == 0u && drones > 0u, "the drone note is within 24..48 at every re-root");
    check(bad_register == 0u, "chord voices 0 and 1 are in 36..82; the melody (voice 2) is in 60..86");
    check(top_on == 0u, "chord voices 0 and 1 never sound the chord's top tone (by the template's third tone, pitch class)");
    check(bad_quiet == 0u, "a chord voice starts a note only when quiet (the melody is not held to quiet)");
    check(double_on == 0u && orphan_off == 0u, "every note-off follows its voice's note-on; no voice starts while it sounds");
    check(max_ons <= ENGINE_VOICES && too_many == 0u, "a call starts at most one note per voice (3 colour voices), and fits its event array");

    /* voice leading: the same matrix at every lead (ENC_KNOB3). The rules above must hold at each, and the notes a chord
       move keeps from the chord before (counted with the literal templates above) must rise at every step */
    {
        static const uint32_t LEADS = 4u;
        uint64_t moves[4] = { 0u, 0u, 0u, 0u }, shared[4] = { 0u, 0u, 0u, 0u };
        uint32_t lead, l_bad = 0u;
        double mean[4];
        for (lead = 0; lead < LEADS; lead++)
            for (si = 0; si < 4u; si++)
                for (sc = 0; sc < 6u; sc++)
                    for (dr = 0; dr < 4u; dr++)
                        for (de = 0; de < 4u; de++) {
                            run_t c;
                            c.seed = SEEDS[si]; c.scale = sc; c.density = de; c.drift = dr; c.lead = lead;
                            c.start = 0u; c.ticks = SIM_TICKS; c.pause_k = 0u; c.resume_k = 0u; c.mute_k = 0u; c.log_on = 0u;
                            sim_run(&s, &c);
                            moves[lead] += s.st.moves;
                            shared[lead] += s.st.shared_sum;
                            l_bad += s.st.bad_scale + s.st.bad_register + s.st.bad_drone + s.st.bad_quiet + s.st.double_on +
                                     s.st.orphan_off + (s.st.max_ons > ENGINE_VOICES) + s.st.too_many;
                        }
        printf("engine: notes a chord move keeps from the chord before, per lead (%llu moves at lead 3):\n", (unsigned long long)moves[3]);
        for (lead = 0; lead < LEADS; lead++) {
            mean[lead] = (double)shared[lead] / (double)moves[lead];
            printf("engine:   lead %u: %.3f of 3 notes kept per move (%llu moves)\n", lead, mean[lead], (unsigned long long)moves[lead]);
        }
        check(l_bad == 0u, "voice leading: at every lead 0-3, notes in scale and register, quiet before a note, no stray offs");
        check(moves[0] > 1000u && moves[3] > 1000u, "voice leading: every lead sees a large number of chord moves");
        /* measured: +0.31, +0.20 and +0.10 per step. 0.08 is a clear step, not noise (the run is deterministic) */
        check(mean[1] - mean[0] >= 0.08 && mean[2] - mean[1] >= 0.08 && mean[3] - mean[2] >= 0.08,
              "voice leading: the notes kept per move rise by at least 0.08 at every lead step");
    }

    /* the note rate: notes per minute per density, over the whole matrix, with the simple envelope model */
    minutes_per_density = (runs / 4u) * (SIM_MS / 60000u);     /* 96 runs of 180 minutes each */
    printf("engine: notes per minute, %u runs per density of 3 simulated hours each:\n", runs / 4u);
    for (d = 0; d < 4u; d++) {
        uint64_t hundredths = notes_by_density[d] * 100u / minutes_per_density;
        printf("engine:   density %u: %u.%02u notes per minute (%llu notes)\n", d, (unsigned)(hundredths / 100u),
               (unsigned)(hundredths % 100u), (unsigned long long)notes_by_density[d]);
    }
    for (d = 1; d < 4u; d++) {
        char what[96];
        snprintf(what, sizeof what, "note rate: density %u has at least %u%% more notes than density %u", (unsigned)d,
                 (unsigned)(100 + RATE_MARGIN_PCT), (unsigned)(d - 1u));
        check(notes_by_density[d] * 100u >= notes_by_density[d - 1u] * (uint64_t)(100 + RATE_MARGIN_PCT), what);
    }

    /* pause: no event while paused, and a note that was held is cut at the pause, not released later */
    {
        run_t c;
        c.seed = 7u; c.scale = 1u; c.density = 3u; c.drift = 1u; c.start = 0u; c.ticks = 4000u;
        c.pause_k = 1200u; c.resume_k = 1400u; c.mute_k = 0u; c.log_on = 0u; c.lead = 0u;
        sim_run(&s, &c);
        check(s.st.paused_events == 0u, "pause: no event at all while paused (10 s, density 3)");
        check(s.st.orphan_off == 0u && s.st.double_on == 0u, "pause: no note-off follows the pause for a note it cut");
    }
    {
        uint32_t held_total = 0u, ok = 1u, i;
        for (i = 0; i < 4u; i++) {
            run_t c;
            c.seed = SEEDS[i]; c.scale = 0u; c.density = 3u; c.drift = 2u; c.start = 0u; c.ticks = 2400u;
            c.pause_k = 1200u; c.resume_k = 1300u; c.mute_k = 0u; c.log_on = 0u; c.lead = 0u;
            sim_run(&s, &c);
            held_total += s.st.held_at_pause;
            if (s.st.orphan_off || s.st.double_on || s.st.paused_events) ok = 0;
        }
        check(held_total > 0u && ok, "pause during held notes: some were held at the pause, and none is re-started or cut wrongly");
    }

    /* resume keeps the remaining time. The pause (500 ms to 10.5 s) comes before any note, so every timer still has time
       left and no fresh wait is drawn. The paused run must then be the unpaused run with its later events moved by 10 s. */
    {
        run_t c;
        uint32_t i, mismatch = 0u;
        c.seed = 99u; c.scale = 2u; c.density = 3u; c.drift = 1u; c.start = 0u; c.ticks = 6000u;
        c.pause_k = 0u; c.resume_k = 0u; c.mute_k = 0u; c.log_on = 1u; c.lead = 0u;
        sim_run(&a, &c);
        c.pause_k = 10u; c.resume_k = 210u;
        c.ticks = 6000u + 200u;                  /* the pause is 200 ticks: B must run that much longer to cover A's end */
        sim_run(&bb, &c);
        /* the chord voices and the drone are compared: the melody is interrupted by a resume (melody_interrupt), so its
           timeline is not the unpaused run's 10 s later, and it has its own test below */
        {
            uint32_t ia = 0u, ib = 0u, na = 0u, nb = 0u;
            for (i = 0; i < a.log_n; i++) na += a.log[i].voice != ENGINE_MELODY_VOICE;
            for (i = 0; i < bb.log_n; i++) nb += bb.log[i].voice != ENGINE_MELODY_VOICE;
            if (na != nb) mismatch = 1;
            for (;;) {
                uint32_t t;
                while (ia < a.log_n && a.log[ia].voice == ENGINE_MELODY_VOICE) ia++;
                while (ib < bb.log_n && bb.log[ib].voice == ENGINE_MELODY_VOICE) ib++;
                if (ia == a.log_n || ib == bb.log_n) break;
                t = a.log[ia].t >= 500u ? a.log[ia].t + 10000u : a.log[ia].t;
                if (bb.log[ib].t != t || bb.log[ib].kind != a.log[ia].kind || bb.log[ib].voice != a.log[ia].voice ||
                    bb.log[ib].note != a.log[ia].note) mismatch = 1;
                if (mismatch) break;
                ia++;
                ib++;
            }
        }
        check(!mismatch && a.log_n > 0u && bb.st.paused_events == 0u,
              "resume: each chord timer restarts with the time it had left (paused run = unpaused run, 10 s later)");
    }

    /* a scale change while paused sends nothing; the drone note waits for resume, in its register and in the new scale */
    {
        engine_t e;
        engine_event_t ev[ENGINE_MAX_EVENTS];
        settings_t set;
        uint32_t n;
        settings_set_defaults(&set);
        n = engine_init(&e, 5u, &set, 0u, ev);
        check(n == 1u && ev[0].kind == EV_DRONE, "init: the boot sends exactly one event, the drone note");
        engine_pause(&e, 1000u);
        set.scale = 3u;
        n = engine_scale_changed(&e, 1500u, &set, ev);
        check(n == 0u, "pause: a scale change while paused sends no event");
        n = engine_tick(&e, 2000u, &set, 7u, ev);
        check(n == 0u, "pause: a tick while paused sends no event, even with every voice due and quiet");
        n = engine_resume(&e, 3000u, &set, ev);
        check(n == 1u && ev[0].kind == EV_DRONE && ev[0].note >= 24u && ev[0].note <= 48u && in_scale(3u, ev[0].note),
              "resume: the deferred drone note is sent at resume, in its register and in the new scale");
    }

    /* resume: every voice is due at the pause (no voice counted as quiet until 60 s, so none started a note), then they
       resume together. No tick may start all three, and none may start on the resume tick itself. A voice with no time
       left draws a fresh wait, which is at least 1200 ms at every density. */
    {
        uint32_t i, dd, ok = 1u;
        for (i = 0; i < 4u; i++)
            for (dd = 0; dd < 4u; dd++) {
                run_t c;
                uint32_t j, resume_t = 1210u * TICK_MS, first = 0xFFFFFFFFu;
                c.seed = SEEDS[i]; c.scale = 1u; c.density = dd; c.drift = 1u; c.start = 0u; c.ticks = 2400u;
                c.pause_k = 1200u; c.resume_k = 1210u; c.mute_k = 1200u; c.log_on = 1u; c.lead = 0u;
                sim_run(&s, &c);
                if (s.st.triple_ticks) ok = 0;
                for (j = 0; j < s.log_n; j++)
                    if (s.log[j].kind == EV_NOTE_ON && s.log[j].t >= resume_t) {
                        first = s.log[j].t;
                        break;
                    }
                if (first == resume_t) ok = 0;
                if (first != 0xFFFFFFFFu && first - resume_t < 1200u) ok = 0;
            }
        check(ok, "resume after every voice was due: no tick starts all three notes, none on the resume tick, first wait >= 1.2 s");
    }

    /* the melody across a pause: a resume starts no melody note for 3 s, and a note sounding at the pause is released at the
       first tick after the resume (the resume tick itself sends nothing). Pauses at 16 points, over 4 seeds, so some pauses
       land in a melody note and some in a rest. */
    {
        uint32_t i, p, cases = 0u, sounding_cases = 0u, ok = 1u;
        for (i = 0; i < 4u; i++)
            for (p = 0; p < 16u; p++) {
                run_t c;
                uint32_t pause_k = 1000u + p * 53u, resume_k = pause_k + 20u, j, last_before = 0xFFFFFFFFu, first_after = 0xFFFFFFFFu;
                uint32_t first_kind = 0u;
                c.seed = SEEDS[i]; c.scale = 1u; c.density = 0u; c.drift = 0u; c.start = 0u; c.ticks = resume_k + 2000u;
                c.pause_k = pause_k; c.resume_k = resume_k; c.mute_k = 0u; c.log_on = 1u; c.lead = 0u;
                sim_run(&s, &c);
                cases++;
                for (j = 0; j < s.log_n; j++) {
                    uint32_t t = s.log[j].t;
                    if (s.log[j].voice != ENGINE_MELODY_VOICE || s.log[j].kind == EV_DRONE) continue;
                    if (t < pause_k * TICK_MS) last_before = s.log[j].kind;
                    else if (t >= resume_k * TICK_MS && first_after == 0xFFFFFFFFu) {
                        first_after = t;
                        first_kind = s.log[j].kind;
                    }
                }
                if (last_before == EV_NOTE_ON) {       /* sounding at the pause: its OFF is the first melody event after the resume */
                    sounding_cases++;
                    if (first_after != (resume_k + 1u) * TICK_MS || first_kind != EV_NOTE_OFF) ok = 0u;
                } else if (first_after != 0xFFFFFFFFu && (first_kind == EV_NOTE_OFF || first_after < resume_k * TICK_MS + 3000u)) {
                    ok = 0u;                    /* silent at the pause: no OFF, and no note within 3 s of the resume */
                }
                if (s.st.paused_events) ok = 0u;
            }
        check(ok && cases == 64u && sounding_cases > 0u,
              "melody across a pause: a note sounding at the pause is released at the first tick after resume; no melody note starts within 3 s");
    }

    /* wrap: fm1_ms passes 0xFFFFFFFF during the run. With the same pause, the run must match the run without the wrap. */
    {
        run_t c;
        uint32_t i, same = 1u;
        c.seed = 1234u; c.scale = 4u; c.density = 2u; c.drift = 3u; c.ticks = 12000u;
        c.pause_k = 3000u; c.resume_k = 3200u; c.mute_k = 0u; c.log_on = 1u; c.lead = 0u;
        c.start = 0u;
        sim_run(&a, &c);
        c.start = 0xFFFFFFFFu - 20000u;         /* the counter wraps 20 s into the run */
        sim_run(&bb, &c);
        if (a.log_n != bb.log_n || a.log_n == 0u) same = 0;
        for (i = 0; same && i < a.log_n; i++)
            if (a.log[i].t != bb.log[i].t || a.log[i].kind != bb.log[i].kind || a.log[i].voice != bb.log[i].voice ||
                a.log[i].note != bb.log[i].note) same = 0;
        check(same, "wrap: a run across fm1_ms = 0xFFFFFFFF gives the same events as the run without the wrap");
        check(bb.st.notes == a.st.notes && bb.st.notes > 0u && bb.st.paused_events == 0u,
              "wrap: the note count across the wrap equals the run without it, and it does not stall");
    }

    printf("engine: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
