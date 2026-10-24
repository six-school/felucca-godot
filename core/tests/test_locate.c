/* M2's locate (docs/DESIGN.md §6.4) on test copies (core/tests/test_hooks.c) sharing one timeline B, all rendering
 * the same chunks from boot (so their blocks fall alike): an instance that joins by locate(origin) must from then on be
 * where one that played continuously since origin is. Compared every chunk of 32 frames, on every track: the step, the
 * samples into it, the passes through LEN (snapshot); the conditions' loop, the nudge latches, an automated PAN.
 * FEL_TEST_LOCATE's pattern: 1/16 x 13 swung, 1/8T x 7 with nudges late and early (QUANTIZE OFF), 1/4 x 5 with PAN
 * automation, DRUM 1/16 x 16 with conditions 2:2 and 1:3; global swing.
 *   A  120 BPM: copy 0 plays from B0; 1 locates at 3.10 s, 2 at 6.77 s; 7 plays from another origin and is relocated
 *      to B0 at 5.00 s; 3 locates to B0 + 12 (ahead) at 1 s, against 4, which play()s at B0 + 12 at 1 s. Exact.
 *   B  copies 5 (plays from B0) and 6 (locates at 7.00 s), the tempo tweened 120 -> 133.7 between 4 and 6 s (every mix
 *      step): continuous play rescaled its phases at each change, locate computes them at the tempo it finds; the
 *      same steps and passes, the positions within 2 samples.
 * And the instance that joins sounds nothing of the step it lands in (copy 1: silent until a step starts), then plays. */
#include "test_hooks.h"
#include "drive.h"
#include "check.h"

const fel_test_t *fel_test_0(void), *fel_test_1(void), *fel_test_2(void), *fel_test_3(void), *fel_test_4(void),
    *fel_test_5(void), *fel_test_6(void), *fel_test_7(void);

#define CH 32u
#define NC 8
typedef struct { const char *what; int copy, ref, exact; double at; uint32_t chunks, bad, pos_max; int on, wait; } cmp_t;
/* on: 0 waiting for its time, 1 located (until it has rendered a block since: wait chunks), 2 compared */

/* copy a against copy b now: 0 alike (pos within tol samples), else 1; *dpos the largest position difference */
static int differ(const fel_test_t *a, const fel_test_t *b, uint32_t tol, uint32_t *dpos)
{
    fel_snapshot_t sa, sb;
    int t, bad = 0;
    a->core->snapshot(&sa);
    b->core->snapshot(&sb);
    bad |= sa.playing != sb.playing;
    for (t = 0; t < FEL_TRACKS; t++) {
        fel_test_track_t ta, tb;
        uint32_t pa = sa.track[t].pos, pb = sb.track[t].pos, d = pa > pb ? pa - pb : pb - pa;
        a->track(t, &ta);
        b->track(t, &tb);
        if (sa.track[t].step == sb.track[t].step) {
            *dpos = d > *dpos ? d : *dpos;
            bad |= sa.track[t].loop != sb.track[t].loop || d > tol;
        } else                                          /* (a step's end within tol: one has crossed it, the other not:
                                                         * the next step, its first samples) */
            bad |= !tol || (pa > tol && pb > tol);
        bad |= ta.cond_loop != tb.cond_loop || ta.pan != tb.pan;
        if (!tol)                                       /* (the latches: at a tempo change continuous play rescales
                                                         * them; where they fall then is within the tolerance) */
            bad |= ta.nd_late != tb.nd_late || ta.nd_early != tb.nd_early || ta.nd_fired != tb.nd_fired;
    }
    return bad;
}

int main(void)
{
    const fel_test_t *x[NC] = {fel_test_0(), fel_test_1(), fel_test_2(), fel_test_3(), fel_test_4(), fel_test_5(),
                               fel_test_6(), fel_test_7()};
    const fel_core_t *cs[NC];
    static float out1[13 * FEL_RATE];
    cmp_t c[] = {
        {"locate at 3.10 s", 1, 0, 1, 3.10, 0, 0, 0, 0, 0},
        {"locate at 6.77 s", 2, 0, 1, 6.77, 0, 0, 0, 0, 0},
        {"playing from elsewhere, relocated at 5.00 s", 7, 0, 1, 5.00, 0, 0, 0, 0, 0},
        {"origin ahead (locate at 1 s to B0 + 12) = play()", 3, 4, 1, 1.00, 0, 0, 0, 0, 0},
        {"after a tempo tween (locate at 7.00 s)", 6, 5, 0, 7.00, 0, 0, 0, 0, 0},
    };
    const int NCMP = (int)(sizeof c / sizeof c[0]);
    uint32_t k, i, n = 12u * FEL_RATE, quiet_end = 0, quiet_from = 0, loud = 0;
    double b0, bt, bpm_b = 120, peak_quiet = 0;
    int j, steps1[FEL_TRACKS], latched[FEL_TRACKS], watching = 0;
    for (k = 0; k < NC; k++) {
        cs[k] = x[k]->core;
        cs[k]->boot();
        x[k]->setup(FEL_TEST_LOCATE);
        cs[k]->tempo(120);
    }
    drive(cs, NC, frames_ms(500), 0);
    b0 = drive_b + 0.37;
    cs[0]->play(b0);
    cs[5]->play(b0);
    cs[7]->play(b0 + 3.3);
    bt = drive_b;
    for (i = 0; i < n; i += CH) {
        double t = (double)i / FEL_RATE;
        if (i % 512u == 0) {                            /* B: copies 5 and 6 on a timeline of their own, tweened */
            bpm_b = t < 4.0 ? 120 : t < 6.0 ? 120 + (133.7 - 120) * (t - 4.0) / 2.0 : 133.7;
            cs[5]->tempo(bpm_b);
            cs[6]->tempo(bpm_b);
        }
        for (j = 0; j < NCMP; j++)
            if (!c[j].on && t >= c[j].at) {
                c[j].on = 1;
                c[j].wait = (int)(256u / CH);
                if (c[j].copy == 3) {
                    CHECK(cs[3]->locate(b0 + 12) == 0, "locate to an origin ahead: taken");
                    cs[4]->play(b0 + 12);
                } else
                    CHECK(cs[c[j].copy]->locate(b0) == 0, "%s: taken", c[j].what);
            }
        {
            double a1 = drive_b + CH * 120.0 / (60.0 * FEL_RATE), b1 = bt + CH * bpm_b / (60.0 * FEL_RATE);
            for (k = 0; k < NC; k++)
                cs[k]->render(CH, k == 5 || k == 6 ? bt : drive_b, k == 5 || k == 6 ? b1 : a1,
                              k == 1 ? out1 + i : 0, 0);
            drive_b = a1;
            bt = b1;
        }
        for (j = 0; j < NCMP; j++)
            if (c[j].on == 1 && --c[j].wait == 0)
                c[j].on = 2;
            else if (c[j].on == 2) {
                c[j].chunks++;
                c[j].bad += (uint32_t)differ(x[c[j].copy], x[c[j].ref], c[j].exact ? 0u : 2u, &c[j].pos_max);
            }
        /* copy 1 just joined: silent until one of its tracks starts a step (no nudged play of the landed one due) */
        if (c[0].on == 2 && c[0].chunks == 1) {
            fel_snapshot_t s;
            cs[1]->snapshot(&s);
            for (j = 0; j < FEL_TRACKS; j++) {
                fel_test_track_t tt;
                x[1]->track(j, &tt);
                steps1[j] = (int)s.track[j].step;
                latched[j] = tt.nd_late || tt.nd_early;
            }
            watching = 1;
            quiet_from = i;
        } else if (watching == 1) {                     /* until a step starts, or a nudged play latched fires (the
                                                         * landed step's late one, the next step's early one) */
            fel_snapshot_t s;
            cs[1]->snapshot(&s);
            for (j = 0; j < FEL_TRACKS; j++) {
                fel_test_track_t tt;
                x[1]->track(j, &tt);
                if ((int)s.track[j].step != steps1[j] || (latched[j] && !tt.nd_late && !tt.nd_early))
                    watching = 2;
            }
            if (watching == 1) {
                peak_quiet = peak_of(out1 + i, CH) > peak_quiet ? peak_of(out1 + i, CH) : peak_quiet;
                quiet_end = i;
            } else
                loud = i;
        }
    }
    for (j = 0; j < NCMP; j++) {
        printf("  %-50s copy %d against %d: %5u chunks compared, %u differ; positions within %u samples\n", c[j].what,
               c[j].copy, c[j].ref, c[j].chunks, c[j].bad, c[j].pos_max);
        CHECK(c[j].chunks > 1000 && c[j].bad == 0, "%s: located = continuous (%u of %u chunks differ)", c[j].what,
              c[j].bad, c[j].chunks);
        if (c[j].exact)
            CHECK(c[j].pos_max == 0, "%s: sample-exact positions (%u)", c[j].what, c[j].pos_max);
    }
    printf("  the step it lands in: %.1f ms silent (peak %.6f) until a step starts or a nudge plays; then peak %.3f"
           " within 300 ms\n",
           (quiet_end - quiet_from) * 1000.0 / FEL_RATE, peak_quiet, loud ? peak_of(out1 + loud, frames_ms(300)) : 0.0);
    CHECK(quiet_end > quiet_from && peak_quiet == 0.0, "locate: nothing of the step it lands in sounds (peak %.6f)",
          peak_quiet);
    CHECK(loud && peak_of(out1 + loud, frames_ms(300)) > 0.01, "locate: the next steps play");
    return check_done("M2 locate (core/tests/test_locate.c)");
}
