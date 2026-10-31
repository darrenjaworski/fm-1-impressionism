/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include "../firmware/src/envelope.h"

int main(void)
{
    env_t e;
    uint32_t i;

    /* test: attack reaches full level instantly */
    env_init(&e);
    assert(e.level == 0);
    env_attack(&e);
    assert(e.level == 32640u);

    /* test: release decays exponentially */
    env_init(&e);
    env_attack(&e);
    uint32_t prev = e.level;
    for (i = 0; i < 100; i++) {
        env_release(&e);
        assert(e.level < prev || e.level == 0);  /* monotonic decay */
        prev = e.level;
    }
    assert(e.level < 32640u / 2);  /* significant decay after 100 blocks */

    /* test: quiet threshold */
    env_init(&e);
    env_attack(&e);
    assert(!env_is_quiet(&e));
    while (e.level >= ENV_QUIET_THRESHOLD) env_release(&e);
    assert(env_is_quiet(&e));

    /* test: T60 roughly 10 seconds (88 blocks per second @ 44.1kHz/256) */
    env_init(&e);
    env_attack(&e);
    uint32_t level_start = e.level;
    for (i = 0; i < 88 * 10; i++) env_release(&e);
    assert(e.level < level_start / 1000u);  /* roughly 60 dB down */

    return 0;
}
