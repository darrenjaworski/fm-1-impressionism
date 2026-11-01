/* SPDX-License-Identifier: GPL-3.0-only */
/* The moods (firmware/src/moods.h) and the save timing rule (settings.h's save_due). No platform dependency:
 * $CC -o build/host/moods_test tests/moods_test.c && build/host/moods_test
 * The factory values are ROADMAP.md's table, written out here as literals: that table is the spec. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/moods.h"
static int fails;
static void check(int ok, const char *what) { printf("moods: %-72s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* ROADMAP.md, item 5: scale, density, drift, colour, drone. (Scales: 0 whole-tone, 1 pentatonic, 2 minor pentatonic,
   4 Mixolydian, 5 Dorian.) Voice leading is not in that table: it is checked for range and distinctness below. */
static const uint8_t ROADMAP[8][5] = {
    { 0, 0, 0, 10, 1 },    /* 1 Voiles */
    { 1, 1, 1, 16, 0 },    /* 2 Clair de lune */
    { 1, 3, 2, 28, 0 },    /* 3 Arabesque No. 1 */
    { 1, 2, 1, 24, 0 },    /* 4 Pagodes */
    { 1, 0, 2, 8, 1 },     /* 5 La cathedrale engloutie */
    { 5, 1, 2, 14, 0 },    /* 6 Sarabande */
    { 4, 1, 1, 12, 1 },    /* 7 Pavane pour une infante defunte */
    { 2, 0, 0, 6, 1 },     /* 8 Le gibet */
};

static int same(const settings_t *a, const settings_t *b)
{
    return a->scale == b->scale && a->density == b->density && a->drift == b->drift && a->color == b->color &&
           a->drone_on == b->drone_on && a->voice_lead == b->voice_lead && a->mood == b->mood;
}

int main(void)
{
    uint32_t m, n, bad_valid = 0, bad_road = 0, bad_name = 0, bad_lead = 0, bad_pair = 0;
    settings_t s, t;

    check(MOOD_COUNT == 8u, "there are 8 moods");

    for (m = 0; m < MOOD_COUNT; m++) {
        settings_set_defaults(&s);
        s.mood = 5u;                          /* the factory fill must set the mood itself */
        mood_factory(&s, m);
        if (!settings_valid(&s) || s.mood != m) bad_valid = 1;
        if (s.scale != ROADMAP[m][0] || s.density != ROADMAP[m][1] || s.drift != ROADMAP[m][2] ||
            s.color != ROADMAP[m][3] || s.drone_on != ROADMAP[m][4]) bad_road = 1;
        if (s.voice_lead > 3u) bad_lead = 1;
    }
    check(!bad_valid, "every mood's factory settings are valid and carry that mood's number");
    check(!bad_road, "the factory scale, density, drift, colour and drone match ROADMAP.md's table, all 8");
    check(!bad_lead, "every mood's voice leading is 0..3");

    for (m = 0; m < MOOD_COUNT; m++)
        for (n = m + 1u; n < MOOD_COUNT; n++) {
            settings_t a, b;
            settings_set_defaults(&a); settings_set_defaults(&b);
            mood_factory(&a, m); mood_factory(&b, n);
            a.mood = b.mood = 0u;
            if (same(&a, &b)) bad_pair = 1;
        }
    check(!bad_pair, "no two moods have the same settings, so no two stops on the dial are the same");

    /* names: they go in the title slot, FONT_L, about 15 characters across the 240 px row */
    for (m = 0; m < MOOD_COUNT; m++) {
        const char *p = mood_piece_name(m), *q = mood_saved_name(m);
        if (!p[0] || strlen(p) > MOOD_NAME_MAX || strlen(q) > MOOD_NAME_MAX || !strcmp(p, q)) bad_name = 1;
    }
    check(!bad_name, "every piece name and saved name is 1 to 15 characters, and a piece name is not its saved name");
    check(!strcmp(mood_saved_name(0), "MOOD 1") && !strcmp(mood_saved_name(7), "MOOD 8"), "saved names read MOOD 1 to MOOD 8");
    check(!strcmp(mood_piece_name(0), "VOILES") && !strcmp(mood_piece_name(7), "LE GIBET"), "piece names: mood 1 is VOILES, mood 8 is LE GIBET");

    /* the dial wraps in both directions, whatever the turn */
    check(mood_step(0u, 1) == 1u && mood_step(3u, -1) == 2u, "mood_step: one detent moves one mood either way");
    check(mood_step(7u, 1) == 0u && mood_step(0u, -1) == 7u, "mood_step: wraps from the last mood to the first and back");
    check(mood_step(2u, 8) == 2u && mood_step(2u, -16) == 2u, "mood_step: a whole number of turns returns to the same mood");
    check(mood_step(0u, -9) == 7u && mood_step(6u, 5) == 3u, "mood_step: a large turn lands on the right mood (0 - 9 = 7, 6 + 5 = 3)");

    /* settings_copy copies all seven fields, and nothing else */
    settings_set_defaults(&s);
    s.scale = 4u; s.density = 2u; s.drift = 3u; s.color = 29u; s.drone_on = 0u; s.voice_lead = 2u; s.mood = 6u;
    memset(&t, 0xAB, sizeof t);
    settings_copy(&t, &s);
    check(same(&t, &s), "settings_copy copies every field (a snapshot equals its source)");

    /* a stored mood blob counts only when its settings are valid and belong to that mood's slot */
    settings_set_defaults(&s);
    s.mood = 3u;
    check(mood_blob_ok(&s, 3u), "mood_blob_ok: valid settings in their own mood's slot are accepted");
    check(!mood_blob_ok(&s, 4u), "mood_blob_ok: a blob whose mood number is another slot's is rejected");
    s.scale = 9u;
    check(!mood_blob_ok(&s, 3u), "mood_blob_ok: out-of-range settings are rejected even in the right slot");
    settings_set_defaults(&s);
    s.mood = 8u;
    check(!mood_blob_ok(&s, 8u), "mood_blob_ok: mood 8 (past the last) is rejected");

    /* save_due: ROADMAP.md's rule. Never in the first 2 s after the change. After that, at once if paused; while playing
       only after 30 s. The ms counter may wrap. */
    check(!save_due(1999u, 0u, 1u) && !save_due(2000u, 0u, 1u), "save_due: not within 2 s of the change (paused)");
    check(save_due(2001u, 0u, 1u), "save_due: paused, just past 2 s, is due");
    check(!save_due(2001u, 0u, 0u) && !save_due(29999u, 0u, 0u) && !save_due(30000u, 0u, 0u), "save_due: playing, not due up to 30 s");
    check(save_due(30001u, 0u, 0u), "save_due: playing, just past 30 s, is due");
    check(save_due(0x00000F00u, 0xFFFFFF00u, 1u) && !save_due(0x00000F00u, 0xFFFFFF00u, 0u),
          "save_due: across an ms counter wrap, 4.1 s on is due paused and not playing");
    check(!save_due(0x00000100u, 0xFFFFFF00u, 1u), "save_due: across a wrap, only 0.5 s on is not due");

    printf("moods: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
