/* SPDX-License-Identifier: GPL-3.0-only */
/* The eight moods on ENC_PRESETS (ROADMAP.md, item 5): each is a set of the saved settings, named after a piece. The
 * factory values below are used while a mood's flash blob is empty. Pure: no platform dependency, integers only, so
 * tests/moods_test.c checks them on the host. */
#pragma once
#include <stdint.h>
#include "settings.h"

#define MOOD_COUNT 8u                           /* one per flash blob (store.h's NBLOB) */
#define MOOD_NAME_MAX 15u                       /* characters: the longest that fits the 240 px title row in FONT_L */

/* per mood: scale (planing.h's SCALE_*), density, drift, colour, drone on, voice leading. The first five are
   ROADMAP.md's table. Voice leading is a starting guess: loose for the busy pieces, held for the still ones. All of it
   is to tune by ear. */
static const uint8_t MOOD_FACTORY[MOOD_COUNT][6] = {
    { 0u, 0u, 0u, 10u, 1u, 2u },                /* Voiles: whole-tone, sparse, still */
    { 1u, 1u, 1u, 16u, 0u, 2u },                /* Clair de lune */
    { 1u, 3u, 2u, 28u, 0u, 0u },                /* Arabesque No. 1: dense, bright, restless */
    { 1u, 2u, 1u, 24u, 0u, 1u },                /* Pagodes */
    { 1u, 0u, 2u, 8u, 1u, 3u },                 /* La cathedrale engloutie: sparse, dark, held */
    { 5u, 1u, 2u, 14u, 0u, 2u },                /* Sarabande: Dorian */
    { 4u, 1u, 1u, 12u, 1u, 2u },                /* Pavane pour une infante defunte: Mixolydian */
    { 2u, 0u, 0u, 6u, 1u, 3u },                 /* Le gibet: minor pentatonic, darkest, held */
};

/* what the screen shows while a mood is as the factory made it (the piece), and once it has been saved over */
static const char *const MOOD_PIECE[MOOD_COUNT] = {
    "VOILES", "CLAIR DE LUNE", "ARABESQUE", "PAGODES", "CATHEDRALE", "SARABANDE", "PAVANE", "LE GIBET" };
static const char *const MOOD_SAVED[MOOD_COUNT] = { "MOOD 1", "MOOD 2", "MOOD 3", "MOOD 4", "MOOD 5", "MOOD 6", "MOOD 7", "MOOD 8" };

static inline const char *mood_piece_name(uint32_t mood) { return MOOD_PIECE[mood]; }
static inline const char *mood_saved_name(uint32_t mood) { return MOOD_SAVED[mood]; }

/* writes mood's factory settings into s, including its number. Field by field: no struct assignment */
static inline void mood_factory(settings_t *s, uint32_t mood)
{
    s->scale = MOOD_FACTORY[mood][0];
    s->density = MOOD_FACTORY[mood][1];
    s->drift = MOOD_FACTORY[mood][2];
    s->color = MOOD_FACTORY[mood][3];
    s->drone_on = MOOD_FACTORY[mood][4];
    s->voice_lead = MOOD_FACTORY[mood][5];
    s->mood = (uint8_t)mood;
}

/* the dial: e detents from this mood (either way, any size), wrapping through all MOOD_COUNT */
static inline uint32_t mood_step(uint32_t mood, int32_t e)
{
    int32_t m = ((int32_t)mood + e) % (int32_t)MOOD_COUNT;
    return (uint32_t)(m < 0 ? m + (int32_t)MOOD_COUNT : m);
}

/* a blob read back from flash counts as mood `mood`'s only when its settings are valid and say they are that mood's */
static inline int mood_blob_ok(const settings_t *s, uint32_t mood)
{
    return settings_valid(s) && s->mood == mood;
}
