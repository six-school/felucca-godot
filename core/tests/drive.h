/* What the tests drive cores with: a conductor's timeline B at a tempo, rendered in mix steps of 512 frames as Godot
 * asks for them (the cores share B), and the measures the checks use. */
#include "fel_core.h"
#include <math.h>
#include <string.h>

static double drive_b, drive_bpm = 120.0;

/* frames of every core in cs (k of them) on the same B; core i's left channel into l[i] (when l and l[i]) */
static inline void drive(const fel_core_t *const *cs, int k, uint32_t frames, float *const *l)
{
    static float scratch_r[512];
    uint32_t done, m;
    int i;
    for (done = 0; done < frames; done += m) {
        double b1;
        m = frames - done < 512u ? frames - done : 512u;
        b1 = drive_b + m * drive_bpm / (60.0 * FEL_RATE);
        for (i = 0; i < k; i++)
            cs[i]->render(m, drive_b, b1, l && l[i] ? l[i] + done : 0, scratch_r);
        drive_b = b1;
    }
}
static inline void drive1(const fel_core_t *c, uint32_t frames, float *l) { drive(&c, 1, frames, &l); }
static inline uint32_t frames_ms(uint32_t t) { return t * (FEL_RATE / 1000u) + t * (FEL_RATE % 1000u) / 1000u; }   /* -> frames */

static inline double rms_of(const float *x, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return n ? sqrt(e / n) : 0;
}
static inline double peak_of(const float *x, uint32_t n)
{
    double p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (fabs(x[i]) > p)
            p = fabs(x[i]);
    return p;
}

/* two snapshots alike, field by field (the struct has padding) */
static inline int snap_same(const fel_snapshot_t *a, const fel_snapshot_t *b)
{
    int t, same = a->playing == b->playing && a->beats == b->beats && a->frames == b->frames;
    for (t = 0; t < FEL_TRACKS; t++)
        same &= a->track[t].step == b->track[t].step && a->track[t].pos == b->track[t].pos &&
                a->track[t].loop == b->track[t].loop;
    return same;
}
