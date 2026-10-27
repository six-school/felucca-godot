/* T6 (docs/DESIGN.md §10) on the core: the delay does not click when the tempo moves its TIME (patch 0003, the
 * glide, D9). A held sine through a 1/4 delay (clicks.c), 20 s, in two test copies alike but for the delay's MIX (127,
 * 0): their difference is the delay's output alone. A jump of its read point shows as a spike of the second
 * difference far above a smooth delayed sine's (wet.py: over 6x its 99th percentile).
 *   constant  128 BPM
 *   int       128, then 120 -> 140 in whole BPM over 2 s (clicks.c's tween), then 140
 *   float     128 +- 12, a sine of 5 s, set every mix step
 * Without the glide (T6's clicks.c) the host clock gives 62 jumps in the int tween. */
#include "test_hooks.h"
#include "drive.h"
#include "check.h"
#include <stdlib.h>

const fel_test_t *fel_test_1(void), *fel_test_2(void), *fel_test_3(void), *fel_test_4(void), *fel_test_5(void),
    *fel_test_6(void);

#define SECS 20u
static float wet_on[SECS * FEL_RATE], wet_off[SECS * FEL_RATE];
static double d2[SECS * FEL_RATE];

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void scenario(const char *name, int prog, const fel_test_t *on, const fel_test_t *off)
{
    const fel_test_t *xs[2] = {on, off};
    float *ls[2] = {wet_on, wet_off};
    uint32_t i, k, n = SECS * FEL_RATE, a = 5u * FEL_RATE, jumps = 0, changes = 0, last_len = 0;
    double last_bpm = 0, p99, big = 0, r = 0;
    drive_timeline = 0;
    for (k = 0; k < 2; k++) {
        xs[k]->core->boot();
        xs[k]->setup(k ? FEL_TEST_DELAY_DRY : FEL_TEST_DELAY);
    }
    for (k = 0; k < 2; k++) {
        xs[k]->core->tempo(128);
        xs[k]->core->play(drive_timeline);
        xs[k]->core->note(0, 57, 100);                  /* A3, held */
    }
    for (i = 0; i < n; i += 512) {
        const fel_core_t *cs[2] = {on->core, off->core};
        float *at[2] = {ls[0] + i, ls[1] + i};
        double t = (double)i / FEL_RATE, bpm = 128;
        if (prog == 1)
            bpm = t < 6 ? 128 : t < 8 ? floor(120 + (t - 6) * 10) : 140;
        else if (prog == 2)
            bpm = 128 + 12 * sin(2 * M_PI * t / 5);
        if (bpm != last_bpm) {
            on->core->tempo(bpm);
            off->core->tempo(bpm);
            last_bpm = bpm;
        }
        drive_bpm = bpm;
        drive(cs, 2, n - i < 512 ? n - i : 512, at);
        if (i >= a && last_len && on->beat_len() != last_len)
            changes++;
        last_len = on->beat_len();
    }
    for (i = a; i < n; i++) {
        double w0 = wet_on[i] - wet_off[i], w1 = wet_on[i - 1] - wet_off[i - 1], w2 = wet_on[i - 2] - wet_off[i - 2];
        d2[i - a] = fabs(w0 - 2 * w1 + w2);
        r += w0 * w0;
    }
    r = sqrt(r / (n - a));
    {
        static double srt[SECS * FEL_RATE];
        memcpy(srt, d2, (n - a) * sizeof *d2);
        qsort(srt, n - a, sizeof *srt, cmp);
        p99 = srt[(n - a) * 99 / 100];
    }
    for (i = 0; i < n - a; i++) {
        if (d2[i] > big) big = d2[i];
        if (d2[i] > 6 * p99) {
            jumps++;
            i += 300;
        }
    }
    printf("  T6 %-8s: wet rms %.3f; beat length changes after 5 s: %3u; jumps: %u; largest %.0fx its 99th percentile\n",
           name, r, changes, jumps, p99 > 0 ? big / p99 : 0.0);
    CHECK(r > 0.01, "T6 %s: the delay sounds (wet rms %.4f)", name, r);
    CHECK(prog == 0 ? changes == 0 : changes > 10, "T6 %s: the tempo moved the delay's TIME (%u changes)", name, changes);
    CHECK(jumps == 0, "T6 %s: no jump in the echoes (%u)", name, jumps);
}

int main(void)
{
    scenario("constant", 0, fel_test_1(), fel_test_2());
    scenario("int", 1, fel_test_3(), fel_test_4());
    scenario("float", 2, fel_test_5(), fel_test_6());
    return check_done("T6 delay glide (core/tests/test_delay.c)");
}
