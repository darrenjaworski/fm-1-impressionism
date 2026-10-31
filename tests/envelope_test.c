/* SPDX-License-Identifier: GPL-3.0-only */
/* The colour-voice envelope (firmware/src/envelope.h): attack, exponential release and the quiet threshold. No platform
 * dependency: $CC -o build/host/envelope_test tests/envelope_test.c && build/host/envelope_test */
#include <stdio.h>
#include "../firmware/src/envelope.h"
static int fails;
static void check(int ok, const char *what) { printf("envelope: %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(void)
{
    env_t e;
    uint32_t i, blocks, prev;
    int bad;

    /* gate on ramps linearly to full, then holds there */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);   /* ceil(32640 / 64) blocks, ~3 s at 172 blocks/s */
    check(e.level == ENV_FULL, "gate on ramps to full level in 510 blocks (about 3 s)");
    env_tick(&e, ENV_ATTACK_STEP);
    check(e.level == ENV_FULL, "the attack never overshoots full level");

    /* gate off decays monotonically and reaches quiet in under 6 s (1032 blocks) */
    env_gate_off(&e);
    prev = e.level;
    bad = 0;
    for (i = 0; i < 100; i++) {
        env_tick(&e, ENV_ATTACK_STEP);
        if (!(e.level < prev || e.level == 0)) bad = 1;
        prev = e.level;
    }
    check(!bad, "after gate off the level falls every block (monotone) for 100 blocks");
    for (blocks = 100; !env_is_quiet(&e) && blocks < 1032; blocks++) env_tick(&e, ENV_ATTACK_STEP);
    check(env_is_quiet(&e), "after gate off the voice reaches quiet within 1032 blocks (under 6 s)");

    /* 60 dB down (to 1/1000 of full) within 6 s of release */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);
    env_gate_off(&e);
    for (i = 0; i < 1032; i++) env_tick(&e, ENV_ATTACK_STEP);
    check(e.level <= ENV_FULL / 1000u, "60 dB down (1/1000 of full) within 6 s of release");

    /* gate on from silence starts from zero, not from full */
    env_init(&e);
    env_gate_on(&e);
    env_tick(&e, ENV_ATTACK_STEP);
    check(e.level == ENV_ATTACK_STEP, "gate on from silence starts from zero, one attack step after one block");

    /* amp scale: full envelope (ENV_FULL = 32640) maps to ~8160 (Q15) */
    env_init(&e);
    env_gate_on(&e);
    for (i = 0; i < 510; i++) env_tick(&e, ENV_ATTACK_STEP);
    check(env_to_amp(&e) == 8160, "full envelope (32640) maps to amp 8160 (Q15)");

    printf("envelope: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
