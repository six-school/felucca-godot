/* A test copy: core/board.c as it is, with FEL_MEASURE (patch 0004's hook after each block's sequencer ticks) and the
 * accessors of core/tests/test_hooks.h, exported as fel_test_<FEL_COPY>() beside fel_core_<FEL_COPY>(). */
#define FEL_MEASURE 1
#include "board.c"
#include "test_hooks.h"

#define TEST_RING 4096u
static fel_test_entry_t test_ring[TEST_RING];
static uint32_t test_w, test_r;
static uint16_t test_last[NTRK];

/* (seq.c, after every track's seq_tick) a track whose step moved entered one in this block */
static void fel_measure_block(uint32_t n)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        if (!song.playing || t->seq_pos >= 0x7FFFFFFFu || t->seq_idx == test_last[k])
            continue;
        test_last[k] = t->seq_idx;
        if (test_w - test_r < TEST_RING)
            test_ring[test_w++ % TEST_RING] = (fel_test_entry_t){hclk_half + hclk_in - n, k, t->seq_idx};
    }
}

static void test_restart(void)
{
    test_r = test_w;
    memset(test_last, 0xFF, sizeof test_last);
}

static uint32_t test_entries(fel_test_entry_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (test_r != test_w && n < max)
        out[n++] = test_ring[test_r++ % TEST_RING];
    return n;
}

static void test_track(int k, fel_test_track_t *o)
{
    const track_t *t = &trk[k & 3];
    o->step = t->seq_idx;
    o->pos = t->seq_pos;
    o->cond_loop = t->cond_loop;
    o->nd_late = t->nd_late;
    o->nd_early = t->nd_early;
    o->nd_fired = t->nd_fired;
    o->pan = t->p[P_PAN];                       /* (FEL_TEST_LOCATE automates it on track 3) */
}

static void test_empty(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SSWING] = 0;
    }
    song.g[G_SWING] = 0;
}

static void test_setup(int which)
{
    static const uint8_t C3[1] = {48}, E3[1] = {52}, G3[1] = {55}, KICK[1] = {36}, HAT[1] = {42}, SNARE[1] = {38};
    track_t *t;
    uint32_t i;
    test_empty();
    if (which == FEL_TEST_TIMING) {             /* T5's three tracks (tempo.c): a 4/4 bar each */
        trk[0].p[P_SDIV] = 2; trk[0].p[P_SLEN] = 16;   /* 1/16 x 16 */
        trk[1].p[P_SDIV] = 0; trk[1].p[P_SLEN] = 4;    /* 1/4 x 4 */
        trk[2].p[P_SDIV] = 4; trk[2].p[P_SLEN] = 12;   /* 1/8T x 12 */
        trk[3].p[P_SDIV] = 2; trk[3].p[P_SLEN] = 16;
    } else if (which == FEL_TEST_DELAY || which == FEL_TEST_DELAY_DRY) {   /* T6 (clicks.c): a held sine through a
                                                                         * 1/4 delay (DRY: its MIX 0), nothing else */
        set_engine_of(&trk[0], 0);              /* ANALOG */
        trk[0].p[P_E0] = 3;                     /* its WAVE (E0, not the LFO's WAVE): SIN */
        trk[0].p[P_E0 + 4] = 127;               /* its CUT */
        trk[0].p[P_SUS] = 127;
        trk[0].p[P_DLY] = 127; trk[0].p[P_REV] = 0; trk[0].p[P_CHOR] = 0;
        song.g[G_DTIME] = 0; song.g[G_DFDBK] = 60; song.g[G_DMIX] = which == FEL_TEST_DELAY ? 127 : 0;
    } else if (which == FEL_TEST_LOCATE) {      /* what locate must reproduce: odd LENs, swing, DIVs, nudges, conditions,
                                                 * automation */
        t = &trk[0];                            /* 1/16 x 13, its own swing 40 (+ the global 15) */
        set_engine_of(t, 0);
        t->p[P_SDIV] = 2; t->p[P_SLEN] = 13; t->p[P_SSWING] = 40;
        put_step(t, 0, 1, C3, ST_NOTE, 0); put_step(t, 3, 1, E3, ST_NOTE, 0); put_step(t, 5, 1, G3, ST_NOTE, 0);
        put_step(t, 8, 1, E3, ST_NOTE, 0); put_step(t, 12, 1, C3, ST_NOTE, 0);
        t = &trk[1];                            /* 1/8T x 7, QUANTIZE OFF: nudges late and early */
        t->p[P_SDIV] = 4; t->p[P_SLEN] = 7; t->p[P_SQNT] = 0;
        for (i = 0; i < 7; i += 2)
            put_step(t, i, 1, G3, ST_NOTE, 0);
        step_set_nudge(&t->step[2], 5); step_set_nudge(&t->step[4], -6); step_set_nudge(&t->step[6], 7);
        t = &trk[2];                            /* 1/4 x 5, PAN automated on steps 1 and 3 */
        t->p[P_SDIV] = 0; t->p[P_SLEN] = 5;
        put_step(t, 0, 1, C3, ST_NOTE, 0); put_step(t, 2, 1, E3, ST_NOTE, 0);
        motion_set_event(t, 1, P_PAN, 40); motion_set_event(t, 3, P_PAN, -30);
        motion_set_enabled(t, 1);
        t = &trk[3];                            /* DRUM 1/16 x 16: conditions 2:2 and 1:3 */
        host_drums(t);
        t->p[P_SDIV] = 2; t->p[P_SLEN] = 16;
        for (i = 0; i < 16; i++)
            put_step(t, i, 1, i % 4 ? HAT : KICK, ST_NOTE, 0);
        put_step(t, 2, 1, SNARE, ST_NOTE, 0); step_set_chance_v(&t->step[2], 100u + 2u);    /* 2:2 */
        put_step(t, 10, 1, SNARE, ST_NOTE, 0); step_set_chance_v(&t->step[10], 100u + 3u);  /* 1:3 */
        song.g[G_SWING] = 15;
    } else if (which == FEL_TEST_TAILS) {       /* what rings on after its notes (the cut's test): no pattern, notes
                                                 * from note() */
        t = &trk[0];                            /* PHYS SYMP, the longest ringing body: its strings 0.8 .. 11 s */
        set_engine_of(t, 9);
        t->p[P_E0] = 3; t->p[P_E0 + 3] = 127; t->p[P_E0 + 4] = 127; t->p[P_E0 + 5] = 127;   /* SYMP, DAMP, SYMP, ACC */
        t->p[P_SUS] = 127; t->p[P_REL] = 127;
        t->p[P_ITYPE] = 7; t->p[P_IC] = 127; t->p[P_IMIX] = 127;   /* INSERT FLANG, full feedback */
        t->p[P_SLCR] = 2; t->p[P_SLPAT] = 13; t->p[P_SLDEPTH] = 127;   /* SLICER STUT: its '.' steps replay */
        t->p[P_DLY] = 127; t->p[P_REV] = 127; t->p[P_CHOR] = 127;
        t = &trk[1];                            /* ANALOG, the longest release, into every bus */
        set_engine_of(t, 0);
        t->p[P_SUS] = 127; t->p[P_REL] = 127;
        t->p[P_DLY] = 127; t->p[P_REV] = 127; t->p[P_CHOR] = 127;
        song.g[G_DTIME] = 0; song.g[G_DFDBK] = 120; song.g[G_DMIX] = 127;   /* a delay fed back past 100 %: it never
                                                                             * dies out on its own */
        song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 127; song.g[G_RDAMP] = 0;   /* HALL, its longest */
    } else if (which == FEL_TEST_KNOCK) {       /* the knock (prototypes/sched.c): DRUM 1/16 x 16 on track 4, closed
                                                 * hats on the off-beats only (every kick heard is a scheduled hit's);
                                                 * ANALOG on track 1, no notes, its CUT half way */
        set_engine_of(&trk[0], 0);
        trk[0].p[P_E0 + 4] = 60;
        for (i = 0; i < 3; i++) {
            trk[i].p[P_SDIV] = 2; trk[i].p[P_SLEN] = 16;
        }
        t = &trk[3];
        host_drums(t);
        t->p[P_SDIV] = 2; t->p[P_SLEN] = 16;
        for (i = 1; i < 16; i += 2)
            put_step(t, i, 1, HAT, ST_NOTE, 0);
    }
    ui.force = 1;
}

_Static_assert(P_E0 + 4 == FEL_TEST_CUT, "test_hooks.h's CUT");

static uint32_t test_frame(void) { return ring_r; }
static uint32_t test_beat_len(void) { return beat_samples(); }

static const fel_test_t FEL_TEST = {&FEL_CORE, test_setup, test_track, test_entries, test_restart, test_frame,
                                    test_beat_len};
const fel_test_t *FEL_CAT(fel_test_, FEL_COPY)(void) { return &FEL_TEST; }
