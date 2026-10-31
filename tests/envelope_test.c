/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "../firmware/src/envelope.h"

int main(void)
{
    env_t e;
    uint32_t i, blocks;

    /* gate on ramps linearly to full, then holds there */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);   /* ceil(32640 / 64) blocks, ~3 s at 172 blocks/s */
    assert(e.level == ENV_FULL);
    env_tick(&e, ENV_ATTACK_STEP);
    assert(e.level == ENV_FULL);                               /* never overshoots */

    /* gate off decays monotonically and reaches quiet in under 6 s (1032 blocks) */
    env_gate_off(&e);
    uint32_t prev = e.level;
    for (i = 0; i < 100; i++) {
        env_tick(&e, ENV_ATTACK_STEP);
        assert(e.level < prev || e.level == 0);
        prev = e.level;
    }
    for (blocks = 100; !env_is_quiet(&e); blocks++) {
        env_tick(&e, ENV_ATTACK_STEP);
        assert(blocks < 1032);
    }

    /* 60 dB down (to 1/1000 of full) within 6 s of release */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);
    env_gate_off(&e);
    for (i = 0; i < 1032; i++) env_tick(&e, ENV_ATTACK_STEP);
    assert(e.level <= ENV_FULL / 1000u);

    /* gate on from silence starts from zero, not from full */
    env_init(&e);
    env_gate_on(&e);
    env_tick(&e, ENV_ATTACK_STEP);
    assert(e.level == ENV_ATTACK_STEP);

    /* amp scale: full envelope is the GATE_FULL peak (8000 ~ 8160) */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);
    assert(env_to_amp(&e) == (int32_t)(ENV_FULL >> 2));

    return 0;
}
