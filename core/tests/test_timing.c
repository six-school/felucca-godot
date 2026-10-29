/* Step timing against the conductor's exact timeline, with its tempo tweened.
 * A test copy (core/tests/test_hooks.c) reports the block each step is entered in (patch 0004's hook); the conductor's
 * timeline at that block's first frame is compared with where the step belongs (play()'s beat + k steps).
 *   A  5 min, an int tween as Godot's (120 -> 140 -> 100 -> 120 in whole BPM, holds between)
 *   B  3 min, a float tempo (110 +- 30, a sine of 23 s: e.g. the player's distance) set every mix step, and a 2 s
 *      pause every 40 s (the conductor's timeline stands still; the tempo moves on meanwhile)
 * A step is entered in the block its time falls in: from 0 to one block (32 samples, 0.73 ms) early, never late, and
 * no drift. At a pause the half rendered ahead (at most 127 frames) plays on, so a step due in it is up to 2.9 ms
 * ahead of the conductor's stopped timeline (the edge). The beat length moves only with the tempo. */
#include "test_hooks.h"
#include "check.h"
#include <math.h>
#include <string.h>

const fel_test_t *fel_test_0(void);

#define MIX 512u
#define MAXMIX (9u * 60u * FEL_RATE / MIX)
static struct { uint32_t f0; double b0, rate, bpm; } mix[MAXMIX];
static uint32_t nmix;
static fel_test_entry_t ent[60000];
static uint32_t nent;
static double timeline;

static void mix_step(const fel_test_t *x, double bpm, int paused)
{
    double b1 = paused ? timeline : timeline + MIX * bpm / (60.0 * FEL_RATE);
    mix[nmix].f0 = x->frame();
    mix[nmix].b0 = timeline;
    mix[nmix].rate = (b1 - timeline) / MIX;
    mix[nmix].bpm = bpm;
    nmix++;
    x->core->render(MIX, timeline, b1, 0, 0);
    timeline = b1;
    nent += x->entries(ent + nent, (uint32_t)(sizeof ent / sizeof ent[0]) - nent);
}
/* the conductor's timeline where frame f is heard (the exact timeline: the mix step it belongs to); *k its mix step */
static double b_at(uint32_t f, uint32_t *k)
{
    uint32_t lo = 0, hi = nmix - 1;
    while (lo < hi) {
        uint32_t m = (lo + hi + 1) / 2;
        if (mix[m].f0 <= f) lo = m; else hi = m - 1;
    }
    *k = lo;
    return mix[lo].b0 + (double)(f - mix[lo].f0) * mix[lo].rate;
}

static double prog_a(double s)                          /* case A's tempo: seconds since play -> whole BPM */
{
    double c = fmod(s, 34.0);
    if (c < 8) return 120;
    if (c < 10) return floor(120 + (c - 8) / 2 * 20);
    if (c < 16) return 140;
    if (c < 20) return ceil(140 - (c - 16) / 4 * 40);
    if (c < 26) return 100;
    if (c < 28) return floor(100 + (c - 26) / 2 * 20);
    return 120;
}

static void run(const fel_test_t *x, const char *name, int prog, double seconds)
{
    static const double SPB[3] = {0.25, 1.0, 1.0 / 3};
    static const uint32_t LEN[3] = {16, 4, 12};
    static const char *const TN[3] = {"1/16", "1/4 ", "1/8T"};
    double origin, last_bpm = 0, emax[3] = {0}, esum[3] = {0}, edge = 0, eearly = 0, elate = 0;
    uint32_t i, n0 = nmix, steps = (uint32_t)(seconds * FEL_RATE / MIX), cnt[3] = {0}, idx_bad = 0, nedge = 0;
    uint32_t len_changes = 0, last_len = 0;
    nent = 0;
    x->restart();
    origin = timeline + 0.25 + 0.0137;                  /* (a quarter ahead, inside some block) */
    x->core->play(origin);
    for (i = 0; i < steps; i++) {
        double t = (double)i * MIX / FEL_RATE, bpm;
        int paused = 0;
        if (prog == 0)
            bpm = prog_a(t);
        else {
            bpm = 110 + 30 * sin(2 * M_PI * t / 23);
            paused = fmod(t, 40.0) >= 20.0 && fmod(t, 40.0) < 22.0;
        }
        if (bpm != last_bpm) {
            x->core->tempo(bpm);
            last_bpm = bpm;
        } else if (prog == 0 && last_len && x->beat_len() != last_len)
            len_changes++;                              /* (the tempo is what it was: the beat length must be too) */
        last_len = x->beat_len();
        mix_step(x, bpm, paused);
    }
    for (i = 0; i < nent; i++) {
        const fel_test_entry_t *e = &ent[i];
        uint32_t k, t = e->track;
        double b, err, ms;
        if (t > 2 || e->frame < mix[n0].f0)
            continue;
        b = b_at(e->frame, &k);
        err = b - (origin + cnt[t] * SPB[t]);           /* beats: <= 0, early */
        ms = err * 60000.0 / mix[k].bpm;
        idx_bad += e->step != cnt[t] % LEN[t];
        cnt[t]++;
        if (mix[k].rate == 0) {                         /* entered in a paused mix step: the half rendered ahead */
            nedge++;
            edge = ms < edge ? ms : edge;
            elate = ms > elate ? ms : elate;
            continue;
        }
        esum[t] += ms;
        if (fabs(ms) > emax[t]) emax[t] = fabs(ms);
        eearly = ms < eearly ? ms : eearly;
        elate = ms > elate ? ms : elate;
    }
    printf("  timing %s, %.0f s:\n", name, seconds);
    for (i = 0; i < 3; i++) {
        double want = (timeline - origin) / SPB[i];
        printf("    %s track: %5u steps (%.1f beats of the conductor's timeline / its length), error max %.3f ms,"
               " mean %.3f ms\n", TN[i], cnt[i], want, emax[i], cnt[i] ? esum[i] / cnt[i] : 0.0);
        CHECK(cnt[i] >= (uint32_t)floor(want) + 1 && cnt[i] <= (uint32_t)floor(want) + 2,
              "timing %s: %s entered %u steps, the conductor's timeline covered %.2f", name, TN[i], cnt[i], want);
    }
    printf("    early at most %.3f ms, late at most %.3f ms; %u steps at a pause's edge, up to %.3f ms ahead\n", -eearly,
           elate, nedge, fabs(edge));
    CHECK(idx_bad == 0, "timing %s: every step entered in order (%u not)", name, idx_bad);
    CHECK(eearly > -(32.0 * 1000 / FEL_RATE) - 0.05 && elate < 0.05,
          "timing %s: each step within one block early (%.3f .. %.3f ms)", name, eearly, elate);
    CHECK(edge > -(128.0 * 1000 / FEL_RATE) - 0.05, "timing %s: at a pause, at most one half ahead (%.3f ms)", name,
          edge);
    if (prog == 0)
        CHECK(len_changes == 0, "timing %s: the beat length moved %u times while the tempo held", name, len_changes);
}

int main(void)
{
    const fel_test_t *x = fel_test_0();
    uint32_t i;
    x->core->boot();
    x->setup(FEL_TEST_TIMING);
    for (i = 0; i < 20; i++)
        mix_step(x, 120, 0);
    run(x, "A (int tween)", 0, 300);
    x->core->stop();
    for (i = 0; i < 20; i++)
        mix_step(x, 120, 0);
    run(x, "B (float tempo, pauses)", 1, 180);
    return check_done("timing (core/tests/test_timing.c)");
}
