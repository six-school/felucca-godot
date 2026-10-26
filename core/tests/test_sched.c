/* M6's scheduler (docs/DESIGN.md §5.1) on test copies (core/tests/test_hooks.c), one timeline (the conductor's):
 *   A  the steps it works out ahead of the sequencer are the steps the sequencer enters: every FEL_EV_STEP against
 *      patch 0004's hook (the block each track entered a step in), on FEL_TEST_LOCATE's pattern (odd LENs, swing,
 *      1/8T with nudges, conditions), 24 s with the tempo tweened (int, then float), a pause and a locate. Exact.
 *   B  the knock (T9 on the core, prototypes/sched.c): KICK hits on steps 1 5 9 13 of the DRUM track's next pass, a
 *      mark and ANALOG's CUT one pass later: each applied in the block its step starts in (that step's own event), the
 *      kicks heard there (the low band's onsets) and nowhere else, the CUT set from there.
 *   C  targets and late policies: the next start of a step (this pass, the next), before step 1, the conductor's
 *      beats; targets a locate jumps past (NEXT: the step's next start, DROP, NOW), a beat target passed (NEXT by its
 *      period, DROP, NOW), a step past LEN; cancel, stop and cut forget; a full table, actions that are not. */
#include "test_hooks.h"
#include "drive.h"
#include "check.h"
#include <stdlib.h>

const fel_test_t *fel_test_0(void), *fel_test_1(void), *fel_test_2(void), *fel_test_3(void);

#define EVN 8192u
static fel_event_t evs[EVN];            /* what a test drained, in order */
static uint32_t nev;

/* frames of c on the timeline, its events drained after every mix step (into evs) */
static void run(const fel_core_t *c, uint32_t frames, float *l)
{
    uint32_t done, m;
    for (done = 0; done < frames; done += m) {
        m = frames - done < 512u ? frames - done : 512u;
        drive1(c, m, l ? l + done : 0);
        nev += c->events(evs + nev, EVN - nev);
    }
}

static const fel_event_t *find(uint32_t kind, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < nev; i++)
        if (evs[i].kind == kind && evs[i].id == id)
            return &evs[i];
    return 0;
}
static const fel_event_t *step_ev(uint32_t track, uint32_t step, uint32_t loop)
{
    uint32_t i;
    for (i = 0; i < nev; i++)
        if (evs[i].kind == FEL_EV_STEP && evs[i].track == track && evs[i].step == step && evs[i].loop == loop)
            return &evs[i];
    return 0;
}

static fel_when_t at_step(uint32_t track, uint32_t step, uint32_t loops, int late)
{
    fel_when_t w = {FEL_AT_STEP, (uint8_t)track, (uint8_t)late, (uint16_t)step, loops, 0, 0};
    return w;
}
static fel_when_t at_beats(double beats, double every, int late)
{
    fel_when_t w = {FEL_AT_BEATS, 0, (uint8_t)late, 0, 0, beats, every};
    return w;
}
static const fel_action_t MARK = {FEL_DO_MARK, 3, 0, 0, 0};

/* ---- A: the steps worked out = the steps entered */
static void test_steps(const fel_test_t *x)
{
    const fel_core_t *c = x->core;
    static fel_test_entry_t ent[4096];
    fel_snapshot_t s;
    uint32_t off, nent, matched = 0, bad = 0, landed = 0, i, j, k, chunks = 24u * FEL_RATE / 512u;
    static uint8_t used[4096];
    double origin;
    c->boot();
    x->setup(FEL_TEST_LOCATE);
    c->tempo(120);
    drive_bpm = 120;
    run(c, frames_ms(300), 0);
    c->snapshot(&s);
    off = x->frame() - (uint32_t)s.frames;             /* the hook counts frames from power-on, events from boot */
    x->restart();
    nev = 0;
    origin = drive_timeline + 0.25;
    c->play(origin);
    for (k = 0; k < chunks; k++) {
        double t = k * 512.0 / FEL_RATE;
        if (t >= 5 && t < 10)                           /* an int tween, 120 -> 97 */
            drive_bpm = (double)(120 - (int)((t - 5) / 5 * 23));
        else if (t >= 10 && t < 12)                     /* the timeline stands (a pause) */
            drive_bpm = 0;
        else if (t >= 12)                               /* a float tween, 97 -> 143.3 */
            drive_bpm = 97 + (t - 12) / 12 * 46.3;
        if (drive_bpm > 0)
            c->tempo(drive_bpm);
        int locating = k == (uint32_t)(15.0 * FEL_RATE / 512);
        uint32_t land_frame = 0;
        if (locating)
            CHECK(c->locate(origin - 2.5) == 0, "locate");   /* (2.5 beats on: a jump) */
        nev = 0;
        run(c, 512, 0);
        nent = x->entries(ent, 4096);
        memset(used, 0, nent);
        for (i = 0; i < nev; i++) {                     /* every step worked out: entered, in its block */
            if (evs[i].kind != FEL_EV_STEP)
                continue;
            for (j = 0; j < nent; j++)
                if (!used[j] && ent[j].track == evs[i].track && ent[j].step == evs[i].step &&
                    ent[j].frame == (uint32_t)evs[i].frame + off)
                    break;
            if (j < nent) {
                used[j] = 1;
                matched++;
            } else if (bad++ < 5)
                printf("  worked out, not entered: track %u step %u at frame %u\n", evs[i].track, evs[i].step,
                       (uint32_t)evs[i].frame + off);
        }
        for (j = 0; j < nent; j++) {                    /* every step entered: worked out. The hook takes a locate's
                                                         * landing (the step moved, none entered) for one: the
                                                         * locate's block only, one a track at most */
            if (used[j])
                continue;
            if (locating && landed < FEL_TRACKS && (!landed || ent[j].frame == land_frame)) {
                land_frame = ent[j].frame;
                landed++;
            } else if (bad++ < 5)
                printf("  entered, not worked out: track %u step %u at frame %u\n", ent[j].track, ent[j].step,
                       ent[j].frame);
        }
    }
    printf("A  %u steps entered in 24 s (tempo tweened, a pause, a locate), %u worked out ahead exactly; the "
           "locate's landing on %u tracks\n", matched + bad, matched, landed);
    CHECK(matched > 400 && bad == 0, "every step worked out ahead is the step entered, in its block (%u wrong)", bad);
    drive_bpm = 120;
}

/* ---- B: the knock */
static void test_knock(const fel_test_t *x)
{
    const fel_core_t *c = x->core;
    static float audio[5 * FEL_RATE], env[5 * FEL_RATE];
    fel_snapshot_t s;
    uint32_t L, i, k, n = (uint32_t)(4.5 * FEL_RATE), nk = 0, onsets = 0, near = 0;
    uint64_t a0, knock[4];
    double y = 0, a = 1 - exp(-2 * M_PI * 120 / FEL_RATE), pk = 0;
    int32_t cut0;
    c->boot();
    x->setup(FEL_TEST_KNOCK);
    c->tempo(120);
    run(c, frames_ms(300), 0);
    c->play(drive_timeline + 0.1);
    run(c, frames_ms(300), 0);                          /* the player knocks somewhere in the first bar */
    c->snapshot(&s);
    L = s.track[3].loop;
    cut0 = c->param_get(0, FEL_TEST_CUT);
    for (i = 0; i < 4; i++) {
        fel_when_t w = at_step(3, i * 4, 1, FEL_LATE_NEXT);
        fel_action_t kick = {FEL_DO_HIT, 3, 0, 100, 0};
        CHECK(c->schedule(&w, &kick, 100 + i) == 0, "schedule a kick");
    }
    {
        fel_when_t w = at_step(3, 0, 2, FEL_LATE_NEXT);
        fel_action_t cut = {FEL_DO_SET, 0, FEL_TEST_CUT, 127, 0};
        CHECK(c->schedule(&w, &MARK, 200) == 0 && c->schedule(&w, &cut, 201) == 0, "schedule the door");
    }
    nev = 0;
    a0 = s.frames;
    run(c, n, audio);
    for (i = 0; i < 4; i++) {
        const fel_event_t *e = find(FEL_EV_ACTION, 100 + i), *st = step_ev(3, i * 4, L + 1);
        CHECK(e && st && e->track == 3 && e->step == i * 4 && e->loop == L + 1 && e->frame == st->frame,
              "kick %u: applied on step %u of pass %u, in that step's block (%s)", i, i * 4 + 1, L + 1,
              e ? "applied" : "missing");
        if (e)
            knock[nk++] = e->frame;
    }
    {
        const fel_event_t *d = find(FEL_EV_ACTION, 200), *cut = find(FEL_EV_ACTION, 201), *st = step_ev(3, 0, L + 2);
        CHECK(d && cut && st && d->loop == L + 2 && d->step == 0 && d->frame == st->frame && cut->frame == st->frame,
              "the door: marked and CUT set on step 1 of pass %u, in its block", L + 2);
        CHECK(cut0 == 60 && c->param_get(0, FEL_TEST_CUT) == 127, "CUT %d -> %d", cut0, c->param_get(0, FEL_TEST_CUT));
    }
    for (k = 0; k < n; k++) {                           /* the low band: one-pole at 120 Hz */
        y += a * (audio[k] - y);
        env[k] = (float)fabs(y);
        pk = env[k] > pk ? env[k] : pk;
    }
    for (k = 1; k < n; k++)
        if (env[k] > pk * 0.3 && env[k - 1] <= pk * 0.3) {
            int64_t at = (int64_t)(a0 + k), best = 1 << 30;
            for (i = 0; i < nk; i++)
                if (llabs(at - (int64_t)knock[i]) < llabs(best))
                    best = at - (int64_t)knock[i];
            printf("B  a low-band onset %+lld samples from the nearest kick's event\n", (long long)best);
            onsets++;
            near += best >= 0 && best < 64;
            k += FEL_RATE * 150 / 1000;
        }
    CHECK(nk == 4 && onsets == 4 && near == 4, "4 kicks heard, each within 64 samples after its event (%u onsets, %u near)",
          onsets, near);
}

/* ---- C: targets, late policies, forgetting, limits */
static void test_targets(const fel_test_t *x, const fel_test_t *y)
{
    const fel_core_t *c = x->core, *d = y->core;
    fel_snapshot_t s;
    const fel_event_t *e;
    fel_when_t w;
    double origin, B, T0, blk = 32.0 * 120 / (60.0 * FEL_RATE);   /* a block, in beats at 120 */
    uint32_t L, I, i, ids[3] = {11, 12, 13};
    int ok;
    drive_bpm = 120;
    /* before step 1 (play() pending): the first pass is the next (copy d on its own: one copy plays at a time) */
    d->boot();
    y->setup(FEL_TEST_KNOCK);
    d->tempo(120);
    run(d, frames_ms(300), 0);
    d->play(drive_timeline + 2.0);
    w = at_step(3, 0, 1, FEL_LATE_NEXT);
    d->schedule(&w, &MARK, 21);
    w = at_step(3, 5, 0, FEL_LATE_NEXT);
    d->schedule(&w, &MARK, 22);
    w = at_step(3, 3, 2, FEL_LATE_NEXT);
    d->schedule(&w, &MARK, 23);
    nev = 0;
    run(d, frames_ms(6000), 0);
    e = find(FEL_EV_ACTION, 21);
    CHECK(e && e->loop == 0 && e->step == 0 && e->frame == step_ev(3, 0, 0)->frame,
          "before step 1: the next pass's step 1 is the first step");
    e = find(FEL_EV_ACTION, 22);
    CHECK(e && e->loop == 0 && e->step == 5, "before step 1: the next start of step 6 is in the first pass");
    e = find(FEL_EV_ACTION, 23);
    CHECK(e && e->loop == 1 && e->step == 3, "before step 1: 2 passes on is the second");
    d->stop();

    c->boot();
    x->setup(FEL_TEST_KNOCK);
    c->tempo(120);
    run(c, frames_ms(300), 0);
    origin = drive_timeline + 0.1;
    c->play(origin);
    run(c, frames_ms(1300), 0);

    /* the next start of a step: this pass while it is still to come, else the next; n passes on */
    c->snapshot(&s);
    L = s.track[3].loop;
    I = s.track[3].step;
    for (i = 0; i < 3; i++) {
        uint32_t S = (I + 15u + i) % 16u;               /* the step before, the step playing, the step after */
        w = at_step(3, S, 0, FEL_LATE_NEXT);
        CHECK(c->schedule(&w, &MARK, ids[i]) == 0, "schedule");
    }
    w = at_step(3, 5, 3, FEL_LATE_NEXT);
    c->schedule(&w, &MARK, 14);
    nev = 0;
    run(c, frames_ms(9000), 0);
    for (i = 0; i < 3; i++) {
        uint32_t S = (I + 15u + i) % 16u, want = L + (S > I ? 0u : 1u);
        e = find(FEL_EV_ACTION, ids[i]);
        CHECK(e && e->step == S && e->loop == want, "next_step %u with step %u of pass %u playing: pass %u (%s %u)", S, I,
              L, want, e ? "got" : "none", e ? e->loop : 0);
    }
    e = find(FEL_EV_ACTION, 14);
    CHECK(e && e->step == 5 && e->loop == L + 3, "3 passes on: pass %u", L + 3);

    /* the conductor's beats: in the block the timeline reaches them */
    B = drive_timeline + 1.2345;
    w = at_beats(B, 0, FEL_LATE_NEXT);
    c->schedule(&w, &MARK, 31);
    nev = 0;
    run(c, frames_ms(1500), 0);
    e = find(FEL_EV_ACTION, 31);
    CHECK(e && e->beats < B && B <= e->beats + blk + 1e-9, "beats %.4f: applied in the block from %.4f", B,
          e ? e->beats : 0);

    /* a locate past resolved targets: NEXT at the step's next start, DROP dropped, NOW at the first step entered */
    c->snapshot(&s);
    L = s.track[3].loop;
    for (i = 0; i < 3; i++) {
        w = at_step(3, 4, 1, (int)i);                   /* FEL_LATE_NEXT, _DROP, _NOW */
        c->schedule(&w, &MARK, 41 + i);
    }
    run(c, 512, 0);                                     /* (resolved: pass L + 1) */
    c->locate(origin - 3 * 4.0 - 1.5);                  /* 3 passes and a half on: past them */
    nev = 0;
    run(c, frames_ms(4000), 0);
    {
        const fel_event_t *first = 0;
        for (i = 0; i < nev && !first; i++)
            if (evs[i].kind == FEL_EV_STEP && evs[i].track == 3)
                first = &evs[i];
        e = find(FEL_EV_ACTION, 41);
        CHECK(first && first->loop > L + 1, "the locate lands past pass %u (pass %u)", L + 1, first ? first->loop : 0);
        CHECK(first && e && e->step == 4 && e->loop == first->loop + (first->step > 4 ? 1u : 0u),
              "NEXT: at step 5's next start after the jump (pass %u)", e ? e->loop : 0);
        e = find(FEL_EV_DROPPED, 42);
        CHECK(first && e && e->frame == first->frame && !find(FEL_EV_ACTION, 42), "DROP: dropped at the first step after it");
        e = find(FEL_EV_ACTION, 43);
        CHECK(first && e && e->frame == first->frame, "NOW: applied at the first step after it");
    }

    /* beat targets passed: NEXT by whole periods, DROP, NOW in the first block that moves */
    T0 = drive_timeline;
    B = T0 - 1.0;
    w = at_beats(B, 4.0, FEL_LATE_NEXT);
    c->schedule(&w, &MARK, 51);
    w = at_beats(B, 4.0, FEL_LATE_DROP);
    c->schedule(&w, &MARK, 52);
    w = at_beats(B, 0, FEL_LATE_NOW);
    c->schedule(&w, &MARK, 53);
    w = at_beats(B, 0, FEL_LATE_NEXT);                  /* (no period: dropped) */
    c->schedule(&w, &MARK, 54);
    nev = 0;
    run(c, frames_ms(3000), 0);
    e = find(FEL_EV_ACTION, 51);
    CHECK(e && e->beats < B + 4 && B + 4 <= e->beats + blk + 1e-9, "a beat passed, NEXT every 4: at %.3f", B + 4);
    CHECK(find(FEL_EV_DROPPED, 52) && !find(FEL_EV_ACTION, 52), "a beat passed, DROP: dropped");
    e = find(FEL_EV_ACTION, 53);
    CHECK(e && fabs(e->beats - T0) < 512.0 * 120 / (60.0 * FEL_RATE), "a beat passed, NOW: in the first mix step (%.4f, "
          "the timeline at %.4f)", e ? e->beats : 0, T0);
    CHECK(find(FEL_EV_DROPPED, 54) && !find(FEL_EV_ACTION, 54), "a beat passed, NEXT without a period: dropped");

    /* a step past LEN (16): dropped when its pass ends */
    w = at_step(3, 20, 0, FEL_LATE_NEXT);
    CHECK(c->schedule(&w, &MARK, 61) == 0, "step 21 schedules (LEN may grow)");
    nev = 0;
    run(c, frames_ms(2500), 0);
    CHECK(find(FEL_EV_DROPPED, 61) && !find(FEL_EV_ACTION, 61), "step 21 of a LEN 16: dropped");

    /* cancel, stop and cut forget; cut empties the events */
    w = at_step(3, 0, 1, FEL_LATE_NEXT);
    c->schedule(&w, &MARK, 71);
    c->schedule(&w, &MARK, 72);
    c->cancel(71);
    nev = 0;
    run(c, frames_ms(2500), 0);
    CHECK(!find(FEL_EV_ACTION, 71) && find(FEL_EV_ACTION, 72), "cancel(71): 71 never applies, 72 does");
    c->schedule(&w, &MARK, 73);
    c->stop();
    c->play(drive_timeline + 0.1);
    nev = 0;
    run(c, frames_ms(4500), 0);
    CHECK(!find(FEL_EV_ACTION, 73), "stop(): forgotten");
    c->schedule(&w, &MARK, 74);
    run(c, 2048, 0);
    c->cut();
    nev = c->events(evs, EVN);
    CHECK(nev == 0, "cut(): no events left (%u)", nev);
    c->play(drive_timeline + 0.1);
    run(c, frames_ms(4500), 0);
    CHECK(!find(FEL_EV_ACTION, 74), "cut(): forgotten");

    /* limits */
    for (i = 0, ok = 1; i < FEL_SCHEDULES; i++)
        ok &= c->schedule(&w, &MARK, 1000 + i) == 0;
    CHECK(ok && c->schedule(&w, &MARK, 2000) == 2, "%d actions held, one more: full", FEL_SCHEDULES);
    c->cancel(0);
    CHECK(c->schedule(&w, &MARK, 2001) == 0, "cancel(0): every one gone");
    {
        fel_action_t lane8 = {FEL_DO_HIT, 3, 8, 100, 0}, vel0 = {FEL_DO_HIT, 3, 0, 0, 0},
                     param = {FEL_DO_SET, 0, 9999, 1, 0}, len0 = {FEL_DO_NOTE, 0, 60, 100, 0},
                     global = {FEL_DO_GLOBAL, 0, -1, 1, 0};
        fel_when_t t4 = at_step(4, 0, 0, FEL_LATE_NEXT), s64 = at_step(3, 64, 0, FEL_LATE_NEXT);
        CHECK(c->schedule(&w, &MARK, 0) == 1 && c->schedule(&w, &lane8, 1) == 1 && c->schedule(&w, &vel0, 1) == 1 &&
                  c->schedule(&w, &param, 1) == 1 && c->schedule(&w, &len0, 1) == 1 &&
                  c->schedule(&w, &global, 1) == 1 && c->schedule(&t4, &MARK, 1) == 1 &&
                  c->schedule(&s64, &MARK, 1) == 1,
              "not actions: id 0, lane 9, velocity 0, a parameter that is none, a note of 0 steps, a global that is "
              "none, track 5, step 65");
    }
}

int main(void)
{
    test_steps(fel_test_0());
    test_knock(fel_test_1());
    test_targets(fel_test_2(), fel_test_3());
    return check_done("test_sched");
}
