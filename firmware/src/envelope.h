/* SPDX-License-Identifier: GPL-3.0-only */
/* Envelope: per-block attack → exponential decay. Level in Q8 (0..32640 = 0..127.5) */

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <stdint.h>

#define ENV_ATTACK_MS 150                       /* attack time: 150 ms */
#define ENV_ATTACK_STEP ((ENV_ATTACK_MS * 44100u / 1000u + 127u) / 256u)  /* per 256-frame block */
#define ENV_DECAY_SHIFT 7                       /* decay: level -= level >> 7 each block. T60 ~6s */
#define ENV_QUIET_THRESHOLD 32                  /* below this (Q8), the voice is "quiet" for retriggering */

typedef struct {
    uint32_t level;                             /* Q8: 0 = silent, 32640 = full (GATE_FULL * 4) */
} env_t;

static inline void env_init(env_t *e) { e->level = 0; }

static inline void env_attack(env_t *e) { e->level = 32640u; }  /* instant to gate, then release */

static inline void env_release(env_t *e)
{
    if (e->level > 0) {
        uint32_t decay = (e->level >> ENV_DECAY_SHIFT);
        if (decay == 0) decay = 1;  /* always decay at least 1 unit per block */
        e->level = e->level > decay ? e->level - decay : 0;
    }
}

static inline int32_t env_to_amp(env_t *e) { return (int32_t)(e->level >> 2u); }  /* Q8 -> Q6 for voice amp */

static inline int env_is_quiet(const env_t *e) { return e->level < ENV_QUIET_THRESHOLD; }

#endif
