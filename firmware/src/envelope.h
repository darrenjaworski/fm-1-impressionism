/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-voice note envelope: a linear attack while the gate is on, then an exponential release once it is off.
   Level in Q8 (0..ENV_FULL = 0..127.5). Advanced once per audio block (172 blocks/s at 44.1 kHz, 256 frames). */

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <stdint.h>

#define ENV_FULL 32640u                         /* Q8 full level = GATE_FULL * 4 */
#define ENV_ATTACK_STEP 64u                     /* Q8 per block: ~3 s to full */
#define ENV_DECAY_SHIFT 7                       /* release: level -= level >> 7 per block, ~5 s to 60 dB down */
#define ENV_QUIET_THRESHOLD 32u                 /* below this (Q8) the voice counts as quiet and may retrigger */

typedef struct {
    volatile uint32_t level;                    /* Q8: 0 = silent, ENV_FULL = full; shared ISR/main */
    volatile uint32_t gate;                     /* 1: note held (attack/sustain), 0: released (decay); shared ISR/main */
} env_t;

static inline void env_init(env_t *e) { e->level = 0; e->gate = 0; }

static inline void env_gate_on(env_t *e) { e->gate = 1; }

static inline void env_gate_off(env_t *e) { e->gate = 0; }

static inline void env_tick(env_t *e, uint32_t attack_step)
{
    if (e->gate) {
        e->level += attack_step;
        if (e->level > ENV_FULL) e->level = ENV_FULL;
    } else if (e->level > 0) {
        uint32_t decay = e->level >> ENV_DECAY_SHIFT;
        if (decay == 0) decay = 1;              /* always decay at least 1 unit per block */
        e->level = e->level > decay ? e->level - decay : 0;
    }
}

static inline int32_t env_to_amp(const env_t *e) { return (int32_t)(e->level >> 2u); }  /* Q8 -> the voice amp scale */

static inline int env_is_quiet(const env_t *e) { return e->level < ENV_QUIET_THRESHOLD; }

#endif
