/* Test copies of the core (core/tests/test_hooks.c, built by core/tests/run.sh as the pool is: renamed per copy): the
 * production table and, beside it, what a test needs to see inside one, as fel_test_<n>(). */
#include "fel_core.h"

typedef struct { uint32_t step, pos, cond_loop, nd_late, nd_early, nd_fired; int32_t pan; } fel_test_track_t;
typedef struct { uint32_t frame, track, step; } fel_test_entry_t;   /* a step entered: the first frame of its block */
enum { FEL_TEST_TIMING = 1, FEL_TEST_DELAY, FEL_TEST_DELAY_DRY, FEL_TEST_LOCATE };   /* setup()'s patterns */

typedef struct {
    const fel_core_t *core;
    void (*setup)(int which);                   /* a test pattern into the tracks (white-box), after boot */
    void (*track)(int t, fel_test_track_t *o);
    uint32_t (*entries)(fel_test_entry_t *out, uint32_t max);   /* steps entered since the last call (ring of 4096) */
    void (*restart)(void);                      /* entries: forget the steps seen (before a play) */
    uint32_t (*frame)(void);                    /* the frame render() returns next (frames count from power-on) */
    uint32_t (*beat_len)(void);                 /* the beat length in samples (the delay, LFO sync, gates) */
} fel_test_t;
