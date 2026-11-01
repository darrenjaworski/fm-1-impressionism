/* SPDX-License-Identifier: GPL-3.0-only */
/* The generative engine's state and rules (ROADMAP.md): the drone's root, the chord's place above it, the chord
 * template, the timers and the colour voices' held flags, and the random walk that moves them. Pure: no platform,
 * no voice or envelope state. Time is passed in as now_ms, and every time comparison is (int32_t)(now - t) >= 0, so
 * the fm1_ms counter may wrap. The functions return events (engine_event_t) that impressionism.c applies to the
 * sound. Integers only, per docs/RULES.md. */
#pragma once
#include <stdint.h>
#include "planing.h"
#include "settings.h"
#include "melody.h"

#define ENGINE_VOICES 3u                        /* the colour voices: 0 and 1 the chord's two lower tones, 2 the melody */
#define ENGINE_CHORD_VOICES 2u                  /* voices 0 and 1 play the chord (the top tone is the melody's anchor) */
#define ENGINE_MELODY_VOICE ENGINE_CHORD_VOICES /* voice 2: the melody (melody.h), a note from its own timer */

/* ---- events: what the engine asks the sound to do ------------------------------------------------------------ */

enum { EV_NOTE_ON = 1, EV_NOTE_OFF = 2, EV_DRONE = 3 };

typedef struct {
    uint8_t kind;                               /* EV_* */
    uint8_t voice;                              /* colour voice 0..2 (EV_NOTE_ON, EV_NOTE_OFF); 0 for EV_DRONE */
    uint8_t note;                               /* MIDI note (EV_NOTE_ON, EV_DRONE); 0 for EV_NOTE_OFF */
    uint8_t level;                              /* EV_NOTE_ON: 255 for a chord note, the melody's level (melody.h) for its notes */
} engine_event_t;

/* One call returns at most this many events: one drone event, at most one note event (on or off) per colour voice,
   and the melody's one note event in place of a colour voice's. The caller's array needs this many entries. */
#define ENGINE_MAX_EVENTS 4u

/* ---- register: MIDI notes, and the limits in semitones above the scale's root (planing.h's clamp_register).
   Drone: ROOT_BASE_NOTE - 12 plus -12..+12 gives notes 24..48. Chord root: ROOT_BASE_NOTE plus -12..+19 gives 36..67,
   and its top voice (at most +15 above the root, quartal) stays at note 82 or below. */
#define ROOT_BASE_NOTE 48u                      /* C3-ish: a sensible register for a drone, tune by ear later */
#define HOME_MIN_ST (-12)
#define HOME_MAX_ST 12
#define ROOT_MIN_ST (-12)
#define ROOT_MAX_ST 19

/* how long a colour note is held before it releases, in ms (an inclusive range, picked per note). Denser settings hold
   for less, so the note rate rises with density: the release time alone would otherwise cap the rate at the top. */
static const struct { uint32_t min_ms, max_ms; } HOLD_TABLE[4] = {
    { 2500u, 7000u },
    { 2500u, 6000u },
    { 2000u, 5000u },
    { 1200u, 3000u },
};

/* the settings' scale count must be planing.h's (a negative array size fails to compile if not) */
typedef char engine_scale_count_matches_planing[SETTINGS_SCALE_COUNT == SCALE_COUNT ? 1 : -1];

typedef struct {
    int32_t home_degree, plane_degree;          /* the drone's root, and the chord's offset above it (scale degrees) */
    uint32_t chord_templ;                       /* planing.h's CHORD_* for the current chord */
    uint32_t rng;                               /* planing.h's xorshift32 state, never 0 */
    uint32_t next_plane_ms, next_root_ms;       /* when the chord next re-planes, and the drone next re-roots */
    uint32_t next_note_ms[ENGINE_CHORD_VOICES]; /* when each chord voice may next trigger (once it is quiet) */
    uint32_t note_off_ms[ENGINE_CHORD_VOICES];  /* when each held chord note releases */
    uint32_t held[ENGINE_CHORD_VOICES];         /* 1: the chord voice's note is on */
    uint32_t drone_note;                        /* the drone's MIDI note, from the last re-root */
    uint32_t drone_pending;                     /* 1: the drone note has not been sent yet (sent on resume while paused) */
    uint32_t paused;                            /* 1: no events, no timers run (BTN_PLAY) */
    int32_t left_plane, left_root;              /* the timers' remaining waits, taken at pause */
    int32_t left_note_ms[ENGINE_CHORD_VOICES];
    melody_t mel;                               /* the melody (voice ENGINE_MELODY_VOICE): its own timer and its own rng */
} engine_t;

/* the drone's MIDI note for this scale and home degree */
static uint32_t engine_drone_note_for(uint32_t scale, int32_t home_degree)
{
    return (uint32_t)((int32_t)ROOT_BASE_NOTE - 12 + scale_degree_to_semitone(scale, home_degree));   /* an octave below the chord */
}

/* sends the drone note once it is pending, unless paused (while paused the drone stays silent, so its note waits for
   resume: no events while paused). Returns the new event count. */
static uint32_t engine_flush_drone(engine_t *e, engine_event_t *ev, uint32_t n)
{
    if (!e->drone_pending || e->paused) return n;
    e->drone_pending = 0;
    ev[n].kind = EV_DRONE;
    ev[n].voice = 0;
    ev[n].note = (uint8_t)e->drone_note;
    ev[n].level = 0;
    return n + 1u;
}

/* the chord now sounding, as a root in semitones from the scale's root (what pick_chord_led compares the next chord to) */
static int32_t engine_chord_root_st(const engine_t *e, uint32_t scale)
{
    return scale_degree_to_semitone(scale, e->home_degree + e->plane_degree);
}

/* the drone re-roots on its own slow timer (60-180 s), using the drift rate for its step size, and the chord goes back
   onto the new root (ROADMAP.md's design choice). lead is the voice leading, 0..3: how strongly the new chord keeps the
   old one's notes. Boot and a scale change pass 0, since there is no old chord to keep (every voice starts afresh). */
static void engine_reroot(engine_t *e, const settings_t *s, uint32_t now_ms, uint32_t lead)
{
    int32_t walked = plan_next(e->home_degree, s->drift, &e->rng);
    int32_t root_old = engine_chord_root_st(e, s->scale);
    e->home_degree = clamp_register(s->scale, walked, HOME_MIN_ST, HOME_MAX_ST);
    e->plane_degree = 0;
    e->chord_templ = pick_chord_led(s->scale, e->home_degree, &e->rng, root_old, e->chord_templ, lead);
    e->drone_note = engine_drone_note_for(s->scale, e->home_degree);
    e->drone_pending = 1;
    e->next_root_ms = now_ms + 60000u + rng_next(&e->rng) % 120001u;   /* 60-180 s: much slower than any plane rate */
}

/* the chord re-planes on the density timer (ENC_ALGORITHM): a new root and chord, no notes. lead as for engine_reroot */
static void engine_replane(engine_t *e, const settings_t *s, uint32_t now_ms, uint32_t lead)
{
    int32_t walked = plan_next(e->plane_degree, s->drift, &e->rng);
    int32_t root = clamp_register(s->scale, e->home_degree + walked, ROOT_MIN_ST, ROOT_MAX_ST);
    int32_t root_old = engine_chord_root_st(e, s->scale);
    e->plane_degree = root - e->home_degree;
    e->chord_templ = pick_chord_led(s->scale, root, &e->rng, root_old, e->chord_templ, lead);
    e->next_plane_ms = now_ms + next_trigger_ticks(s->density, &e->rng);
}

static uint32_t engine_emit_note(engine_event_t *ev, uint32_t n, uint32_t kind, uint32_t v, uint32_t note, uint32_t level)
{
    ev[n].kind = (uint8_t)kind;
    ev[n].voice = (uint8_t)v;
    ev[n].note = (uint8_t)note;
    ev[n].level = (uint8_t)level;
    return n + 1u;
}

/* sets up the engine at boot: a real root and chord in place, every chord voice's first wait, the drone note, and the
   melody's first statement (melody.h). seed must not be 0 (0 is mapped to 1). The melody's rng is seeded from a
   different value, so its draws are its own. Returns the event count (at most 1). */
static uint32_t engine_init(engine_t *e, uint32_t seed, const settings_t *s, uint32_t now_ms, engine_event_t *ev)
{
    uint32_t v;
    e->rng = seed ? seed : 1u;
    e->home_degree = 0;
    e->plane_degree = 0;
    e->chord_templ = 0u;
    e->next_plane_ms = now_ms;
    e->next_root_ms = now_ms;
    e->drone_note = 0u;
    e->drone_pending = 0u;
    e->paused = 0u;
    e->left_plane = 0;
    e->left_root = 0;
    for (v = 0; v < ENGINE_CHORD_VOICES; v++) {
        e->next_note_ms[v] = now_ms;
        e->note_off_ms[v] = now_ms;
        e->held[v] = 0u;
        e->left_note_ms[v] = 0;
    }
    melody_init(&e->mel, seed ^ 0x5DEECE66u, now_ms);
    engine_reroot(e, s, now_ms, 0u);
    engine_replane(e, s, now_ms, 0u);
    for (v = 0; v < ENGINE_CHORD_VOICES; v++)
        e->next_note_ms[v] = now_ms + next_trigger_ticks(s->density, &e->rng);
    return engine_flush_drone(e, ev, 0u);
}

/* one main-loop step (not while paused). quiet_mask bit v is set when colour voice v's envelope is quiet, so it may
   take a new note; the engine uses bits 0 and 1 only (the melody is not held to quiet). Order: the drone's re-root,
   the chord's re-plane, each chord voice (note off, or note on), then the melody's one event. */
static uint32_t engine_tick(engine_t *e, uint32_t now_ms, const settings_t *s, uint32_t quiet_mask, engine_event_t *ev)
{
    uint32_t v, n = 0u, note = 0u, level = 0u, what;
    if (e->paused) return 0u;
    if ((int32_t)(now_ms - e->next_root_ms) >= 0) engine_reroot(e, s, now_ms, s->voice_lead);
    if ((int32_t)(now_ms - e->next_plane_ms) >= 0) engine_replane(e, s, now_ms, s->voice_lead);
    for (v = 0; v < ENGINE_CHORD_VOICES; v++) {
        if (e->held[v]) {
            if ((int32_t)(now_ms - e->note_off_ms[v]) >= 0) {
                e->held[v] = 0u;
                n = engine_emit_note(ev, n, EV_NOTE_OFF, v, 0u, 255u);
                e->next_note_ms[v] = now_ms + next_trigger_ticks(s->density, &e->rng);
            }
        } else if (((quiet_mask >> v) & 1u) && (int32_t)(now_ms - e->next_note_ms[v]) >= 0) {
            int32_t chord_note = (int32_t)ROOT_BASE_NOTE + scale_degree_to_semitone(s->scale, e->home_degree + e->plane_degree);
            uint32_t lo = HOLD_TABLE[s->density].min_ms, hi = HOLD_TABLE[s->density].max_ms;
            e->held[v] = 1u;
            e->note_off_ms[v] = now_ms + lo + rng_next(&e->rng) % (hi - lo + 1u);
            n = engine_emit_note(ev, n, EV_NOTE_ON, v,
                                 (uint32_t)(chord_note + chord_voice_semitone(e->chord_templ, v)), 255u);
        }
    }
    what = melody_tick(&e->mel, now_ms, s->scale, e->home_degree + e->plane_degree, e->chord_templ,
                       scale_degree_to_semitone(s->scale, e->home_degree), s->density, &note, &level);
    if (what == MELODY_ON) n = engine_emit_note(ev, n, EV_NOTE_ON, ENGINE_MELODY_VOICE, note, level);
    else if (what == MELODY_OFF) n = engine_emit_note(ev, n, EV_NOTE_OFF, ENGINE_MELODY_VOICE, 0u, 0u);
    return engine_flush_drone(e, ev, n);
}

/* BTN_PLAY, pause: freezes every timer (remembering how long each has left), and releases the held flags. The caller
   fades the sound out. The melody is not told: its state waits, and resume interrupts it. */
static void engine_pause(engine_t *e, uint32_t now_ms)
{
    uint32_t v;
    e->left_plane = (int32_t)(e->next_plane_ms - now_ms);
    e->left_root = (int32_t)(e->next_root_ms - now_ms);
    for (v = 0; v < ENGINE_CHORD_VOICES; v++) {
        e->left_note_ms[v] = (int32_t)(e->next_note_ms[v] - now_ms);
        e->held[v] = 0u;
    }
    e->paused = 1u;
}

/* BTN_PLAY, resume: each timer restarts with the time it had left. A chord voice with no wait left (it was due while
   paused, or it was holding a note) gets a fresh random wait instead of entering at once: otherwise both chord voices
   that were due would enter together. The melody is interrupted: a note that was sounding is released at the next tick,
   and the melody rests 3-6 s before it returns, so it never resumes mid-phrase or at once. Returns the event count. */
static uint32_t engine_resume(engine_t *e, uint32_t now_ms, const settings_t *s, engine_event_t *ev)
{
    uint32_t v;
    e->paused = 0u;
    e->next_plane_ms = now_ms + (uint32_t)(e->left_plane > 0 ? e->left_plane : 0);
    e->next_root_ms = now_ms + (uint32_t)(e->left_root > 0 ? e->left_root : 0);
    for (v = 0; v < ENGINE_CHORD_VOICES; v++) {
        if (e->left_note_ms[v] > 0) e->next_note_ms[v] = now_ms + (uint32_t)e->left_note_ms[v];
        else e->next_note_ms[v] = now_ms + next_trigger_ticks(s->density, &e->rng);
    }
    melody_interrupt(&e->mel, now_ms);
    return engine_flush_drone(e, ev, 0u);
}

/* ENC_SELECT moved the scale: home and chord reset to degree 0, every chord voice is due now, the drone and chord are
   re-planed at once, and the melody is interrupted (a note in the old scale is released, then a rest). Returns the
   event count (the drone note, when not paused). */
static uint32_t engine_scale_changed(engine_t *e, uint32_t now_ms, const settings_t *s, engine_event_t *ev)
{
    uint32_t v;
    e->home_degree = 0;
    e->plane_degree = 0;
    for (v = 0; v < ENGINE_CHORD_VOICES; v++) e->next_note_ms[v] = now_ms;
    melody_interrupt(&e->mel, now_ms);
    engine_reroot(e, s, now_ms, 0u);
    engine_replane(e, s, now_ms, 0u);
    return engine_flush_drone(e, ev, 0u);
}
