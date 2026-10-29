/* The scheduler (fel_core.h: schedule, cancel, events) on test copies (core/tests/test_hooks.c), one timeline (the
 * conductor's):
 *   A  the steps it works out ahead of the sequencer are the steps the sequencer enters: every FEL_EV_STEP against
 *      patch 0004's hook (the block each track entered a step in), on FEL_TEST_LOCATE's pattern (odd LENs, swing,
 *      1/8T with nudges, conditions), 24 s with the tempo tweened (int, then float), a pause and a locate. Exact. And
 *      each step's length in beats: swung, a long step and the short one after it make two of its DIV's.
 *   B  the knock (demo/knock.gd's idea): KICK hits on steps 1 5 9 13 of the DRUM track's next pass, a mark and
 *      ANALOG's CUT one pass later: each applied in the block its step starts in (that step's own event), the
 *      kicks heard there (the low band's onsets) and nowhere else, the CUT set from there.
 *   C  targets and late policies: the next start of a step (this pass, the next), before step 1, the conductor's
 *      beats; targets a locate jumps past (NEXT: the step's next start, DROP, NOW), a beat target passed (NEXT by its
 *      period, DROP, NOW), a step past LEN; cancel, stop and cut forget; a full table, actions that are not.
 *   D  notes (patch 0005): a DRUM track's hits (its steps', the scheduled ones) reported in their steps' blocks
 *      with their lanes, a synth's note with none, a muted track's nothing; every note on paired with one off: the
 *      sequencer's at their gates' ends and at a stop, held keys (MIDI IN) when released or by a panic (trk_all_off),
 *      not by a stop.
 *   E  replace and mask (patch 0006), counted on every note started (before the events' pairing): a HIT in
 *      place of its lane on the step (as a note, as a lane hit; the step's other lanes play), a NOTE in place of the
 *      step's notes (a ratchet's three too), a layered one over them, a step masked whose note is nudged late or
 *      early, a loop masked but for a scheduled hit, a bar of a triplet part (two of its loops) against one loop of
 *      it, a mask from a bar's beat, a mask cancelled before it starts (it never does) and while running (it runs on);
 *      each against the same step a pass later, as programmed.
 *   F  a SET for an engine: applied while the track plays it; for another engine, or one the track has left
 *      before the step comes, dropped (an engine's parameter id is every engine's: it would move another's). */
#include "test_hooks.h"
#include "drive.h"
#include "check.h"
#include <stdlib.h>

const fel_test_t *fel_test_0(void), *fel_test_1(void), *fel_test_2(void), *fel_test_3(void), *fel_test_4(void),
    *fel_test_5(void), *fel_test_6(void);

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
    uint32_t pairs = 0, pairs_bad = 0, swung = 0, last_step[FEL_TRACKS] = {99, 99, 99, 99};
    double last_len[FEL_TRACKS] = {0};
    static const double BASE[FEL_TRACKS] = {0.25, 1.0 / 3.0, 1.0, 0.25};   /* FEL_TEST_LOCATE: 1/16, 1/8T, 1/4, 1/16 */
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
            {                                           /* (its length: a swung pair makes two DIVs) */
                uint32_t t = evs[i].track;
                if (evs[i].step % 2u && evs[i].step == last_step[t] + 1u) {
                    pairs++;
                    pairs_bad += fabs(last_len[t] + evs[i].length - 2.0 * BASE[t]) > 2e-3 * BASE[t];   /* (the swing is
                                                         * whole samples of the step's tempo: the tween moves it) */
                    swung += last_len[t] > BASE[t] + 1e-6;
                }
                last_step[t] = evs[i].step;
                last_len[t] = evs[i].length;
            }
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
    printf("A  step lengths: %u of %u swung pairs (a long step and its short one) make two of their DIV's (within "
           "0.1 %%), %u longer than unswung\n", pairs - pairs_bad, pairs, swung);
    CHECK(pairs > 150 && pairs_bad == 0 && swung == pairs, "each swung pair two DIVs long, its first step the longer (%u "
          "wrong of %u)", pairs_bad, pairs);
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
        fel_action_t cut = {FEL_DO_SET, 0, FEL_TEST_CUT, 127, 0};   /* (for engine 0, ANALOG: track 1's) */
        CHECK(c->schedule(&w, &MARK, 200) == 0 && c->schedule(&w, &cut, 201) == 0, "schedule the door");
    }
    nev = 0;
    a0 = s.frames;
    run(c, n, audio);
    for (i = 0, k = 0; i < nev; i++)
        k += evs[i].kind == FEL_EV_STEP && evs[i].length != 0.25;
    CHECK(k == 0, "unswung 1/16: every step 0.25 beats long (%u not)", k);
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

/* ---- D: notes */
static void test_notes(const fel_test_t *x)
{
    static const uint8_t LANE_NOTE[8] = {36, 38, 39, 42, 46, 45, 37, 56};   /* eng_drum.c DRUM_LANE_NOTE */
    const fel_core_t *c = x->core;
    fel_snapshot_t s;
    uint32_t i, k, L, hats = 0, hats_bad = 0, held[FEL_TRACKS][128] = {{0}}, unpaired = 0, doubled = 0, left = 0;
    int synth_on = 0, synth_off = 0, muted = 0;
    c->boot();
    x->setup(FEL_TEST_KNOCK);
    c->tempo(120);
    run(c, frames_ms(300), 0);
    c->play(drive_timeline + 0.1);
    run(c, frames_ms(300), 0);
    c->snapshot(&s);
    L = s.track[3].loop;
    for (i = 0; i < 2; i++) {
        fel_when_t w = at_step(3, i * 4, 1, FEL_LATE_NEXT);
        fel_action_t kick = {FEL_DO_HIT, 3, 0, 100, 0};
        c->schedule(&w, &kick, 900 + i);
    }
    nev = 0;
    c->note(0, 99, 90);                                 /* a synth note (ANALOG on track 1, no pattern), held */
    run(c, frames_ms(500), 0);
    c->note(0, 99, 0);
    run(c, frames_ms(500), 0);
    c->param_set(0, FEL_TEST_MUTE, 1);                  /* muted: neither on nor off */
    c->note(0, 98, 90);
    run(c, frames_ms(200), 0);
    c->note(0, 98, 0);
    run(c, frames_ms(3000), 0);
    c->param_set(0, FEL_TEST_MUTE, 0);
    c->note(0, 97, 90);                                 /* two keys held over a stop (a stop releases the
                                                         * sequencer's notes, not keys held) */
    c->note(0, 96, 90);
    run(c, frames_ms(100), 0);
    c->stop();
    run(c, frames_ms(100), 0);
    for (i = 0, k = 0; i < nev; i++)
        if (evs[i].track == 0 && evs[i].note >= 96)
            k += evs[i].kind == FEL_EV_NOTE ? 1u : evs[i].kind == FEL_EV_NOTE_OFF ? (uint32_t)-1 : 0u;
    CHECK(k == 2, "two keys held over a stop: still on (%u)", k);
    x->panic();                                         /* .. until a panic (trk_all_off) */
    run(c, frames_ms(100), 0);
    for (i = 0; i < nev; i++) {
        const fel_event_t *e = &evs[i];
        if (e->kind == FEL_EV_NOTE) {
            doubled += held[e->track][e->note]++ != 0;
            if (e->track == 3 && e->note == 42) {       /* the hats, on the odd steps */
                const fel_event_t *st = step_ev(3, e->step, e->loop);
                hats++;
                hats_bad += !(e->lane == 3 && e->step % 2 && st && st->frame == e->frame);
            }
            synth_on += e->track == 0 && e->note == 99 && e->lane == -1 && e->velocity == 90;
            muted += e->track == 0 && e->note == 98;
        } else if (e->kind == FEL_EV_NOTE_OFF) {
            unpaired += held[e->track][e->note] == 0;
            held[e->track][e->note] -= held[e->track][e->note] != 0;
            synth_off += e->track == 0 && e->note == 99;
            muted += e->track == 0 && e->note == 98;
        }
    }
    for (i = 0; i < FEL_TRACKS * 128u; i++)
        if (held[i / 128u][i % 128u]) {
            left += held[i / 128u][i % 128u];
            printf("D  left on: track %u note %u\n", i / 128u + 1, i % 128u);
        }
    printf("D  %u hats reported, %s\n", hats, hats_bad ? "some wrong" : "each with lane HAT CL in its odd step's block");
    CHECK(hats >= 14 && hats_bad == 0, "the hats: lane 3 (HAT CL), in their steps' blocks (%u of %u wrong)", hats_bad, hats);
    for (i = 0; i < 2; i++) {
        const fel_event_t *a = find(FEL_EV_ACTION, 900 + i), *n = 0;
        uint32_t j;
        for (j = 0; j < nev && a; j++)
            if (evs[j].kind == FEL_EV_NOTE && evs[j].track == 3 && evs[j].note == LANE_NOTE[0] && evs[j].frame == a->frame)
                n = &evs[j];
        CHECK(a && n && n->lane == 0 && n->velocity == 100 && a->loop == L + 1,
              "scheduled kick %u: its note (36, lane KICK, velocity 100) in its action's block", i);
    }
    CHECK(synth_on == 1 && synth_off == 1, "a synth's note: on (99, velocity 90, no lane) and off, once each");
    CHECK(muted == 0, "a muted track's note: neither on nor off");
    CHECK(doubled == 0 && unpaired == 0 && left == 0, "every note on paired with one off, the panic's too (%u on twice, "
          "%u off without on, %u left on)", doubled, unpaired, left);
}

/* ---- E: replace and mask */
#define ONN 8192u
static fel_test_note_t ons[ONN];
static uint32_t nons;

static void run_m(const fel_test_t *x, uint32_t frames)
{
    uint32_t done, m;
    for (done = 0; done < frames; done += m) {
        m = frames - done < 512u ? frames - done : 512u;
        run(x->core, m, 0);
        nons += x->notes(ons + nons, ONN - nons);
    }
}

/* the frames of step (track, step, loop): from its block to the track's next step's (0: none) */
static int window(uint32_t track, uint32_t step, uint32_t loop, uint64_t *from, uint64_t *to)
{
    uint32_t i;
    const fel_event_t *e = step_ev(track, step, loop);
    if (!e)
        return 0;
    *from = e->frame;
    *to = (uint64_t)-1;
    for (i = (uint32_t)(e - evs) + 1u; i < nev; i++)
        if (evs[i].kind == FEL_EV_STEP && evs[i].track == track) {
            *to = evs[i].frame;
            break;
        }
    return 1;
}

/* notes started on track, note (-1: any), velocity (-1: any) within [from, to) */
static uint32_t count_ons(uint32_t track, int note, int vel, uint64_t from, uint64_t to)
{
    uint32_t i, n = 0;
    for (i = 0; i < nons; i++)
        n += ons[i].track == track && (note < 0 || ons[i].note == (uint32_t)note) && (vel < 0 || ons[i].vel == (uint32_t)vel)
             && ons[i].frame >= from && ons[i].frame < to;
    return n;
}

/* .. in step (track, step, loop) */
static uint32_t in_step(uint32_t track, uint32_t step, uint32_t loop, int note, int vel)
{
    uint64_t a, b;
    return window(track, step, loop, &a, &b) ? count_ons(track, note, vel, a, b) : 9999u;
}

/* .. in pass loop of track */
static uint32_t in_pass(uint32_t track, uint32_t loop, int note)
{
    uint64_t a, b, c, d;
    if (!window(track, 0, loop, &a, &b) || !window(track, 0, loop + 1u, &c, &d))
        return 9999u;
    return count_ons(track, note, -1, a, c);
}

static void test_mask(const fel_test_t *x)
{
    const fel_core_t *c = x->core;
    fel_snapshot_t s;
    uint32_t L, L2, i, ok, k2[5];
    double origin;
    fel_when_t w;
    c->boot();
    x->setup(FEL_TEST_MASK);
    c->tempo(120);
    drive_bpm = 120;
    run(c, frames_ms(300), 0);
    origin = drive_timeline + 0.1;
    c->play(origin);
    nev = nons = 0;
    run_m(x, frames_ms(300));
    c->snapshot(&s);
    L = s.track[0].loop;                                /* (tracks 1, 2, 4: 1/16 x 16, in step) */
    L2 = s.track[2].loop;                               /* (track 3: 1/8T x 6, a pass a second) */
    {
        fel_action_t kick = {FEL_DO_HIT, 3, 0, 50, 0}, snare = {FEL_DO_HIT, 3, 1, 50, 0},
                     hi = {FEL_DO_NOTE, 0, 72, 90, 1}, over = {FEL_DO_NOTE_OVER, 0, 72, 90, 1},
                     one = {FEL_DO_MASK, 1, 1, FEL_SPAN_STEPS, 0}, loop3 = {FEL_DO_MASK, 3, 1, FEL_SPAN_LOOPS, 0},
                     kick70 = {FEL_DO_HIT, 3, 0, 70, 0}, bar2 = {FEL_DO_MASK, 2, 1, FEL_SPAN_BARS, 0},
                     loop2 = {FEL_DO_MASK, 2, 1, FEL_SPAN_LOOPS, 0}, step0 = {FEL_DO_MASK, 0, 1, FEL_SPAN_STEPS, 0},
                     loop0 = {FEL_DO_MASK, 0, 1, FEL_SPAN_LOOPS, 0}, two0 = {FEL_DO_MASK, 0, 2, FEL_SPAN_STEPS, 0};
        w = at_step(3, 0, 1, FEL_LATE_NEXT); c->schedule(&w, &kick, 1101);      /* E1: kick (with a hat), step 1 */
        w = at_step(3, 4, 1, FEL_LATE_NEXT); c->schedule(&w, &snare, 1102);     /* E2: snare (a lane hit), step 5 */
        w = at_step(0, 4, 1, FEL_LATE_NEXT); c->schedule(&w, &hi, 1103);        /* E3: the ratcheted step 5 */
        w = at_step(0, 8, 1, FEL_LATE_NEXT); c->schedule(&w, &over, 1104);      /* E4: layered on step 9 */
        w = at_step(1, 6, 1, FEL_LATE_NEXT); c->schedule(&w, &one, 1105);       /* E5: step 7, nudged early */
        w = at_step(1, 2, 1, FEL_LATE_NEXT); c->schedule(&w, &one, 1106);       /* .. step 3, nudged late */
        w = at_step(3, 0, 2, FEL_LATE_NEXT); c->schedule(&w, &loop3, 1107);     /* E6: a loop masked */
        w = at_step(3, 8, 2, FEL_LATE_NEXT); c->schedule(&w, &kick70, 1108);    /* .. but a kick in it */
        w = at_step(2, 0, 1, FEL_LATE_NEXT); c->schedule(&w, &bar2, 1109);      /* E7: a bar of 1/8T: 2 loops */
        w = at_step(2, 0, 4, FEL_LATE_NEXT); c->schedule(&w, &loop2, 1110);     /* .. one loop */
        w = at_step(0, 0, 3, FEL_LATE_NEXT); c->schedule(&w, &step0, 1111);     /* E8: cancelled before */
        c->cancel(1111);
        w = at_step(0, 0, 4, FEL_LATE_NEXT); c->schedule(&w, &loop0, 1112);     /* .. cancelled while running */
        w = at_beats(origin + 4.0 * (L + 5), 0, FEL_LATE_NEXT); c->schedule(&w, &two0, 1113);   /* E9: from a beat */
    }
    run_m(x, frames_ms(9100));                          /* (into pass L + 4) */
    c->cancel(1112);
    run_m(x, frames_ms(4000));
    CHECK(in_step(3, 0, L + 1, 36, -1) == 1 && in_step(3, 0, L + 1, 36, 50) == 1 && in_step(3, 0, L + 1, 42, -1) == 1,
          "E1 a kick hit on a step of a kick and a hat (as notes): one kick, the hit's (velocity 50), the hat plays");
    CHECK(in_step(3, 4, L + 1, 38, -1) == 1 && in_step(3, 4, L + 1, 38, 50) == 1 && in_step(3, 4, L + 1, 42, -1) == 1,
          "E2 a snare hit on a step of a snare (a lane hit) and a hat: one snare, the hit's, the hat plays");
    CHECK(in_step(3, 0, L + 3, 36, -1) == 1 && in_step(3, 0, L + 3, 36, 100) == 1,
          ".. and when not replaced, its own kick (the step's velocity, 100)");
    CHECK(in_step(0, 4, L + 1, 48, -1) == 0 && in_step(0, 4, L + 1, 72, -1) == 1 && in_step(0, 4, L + 2, 48, -1) == 3,
          "E3 a note on a ratcheted step: none of its 3 (%u), the note; a pass later its 3 (%u)", in_step(0, 4, L + 1, 48, -1),
          in_step(0, 4, L + 2, 48, -1));
    CHECK(in_step(0, 8, L + 1, 48, -1) == 1 && in_step(0, 8, L + 1, 72, -1) == 1, "E4 a layered note: the step's and the note");
    {
        uint64_t a, b, c2, d;
        ok = window(1, 5, L + 1, &a, &b) && window(1, 7, L + 1, &c2, &d);
        CHECK(ok && count_ons(1, 52, -1, a, c2) == 0, "E5 a step masked whose note plays early (in the step before): silent");
        ok = window(1, 5, L + 2, &a, &b) && window(1, 7, L + 2, &c2, &d);
        CHECK(ok && count_ons(1, 52, -1, a, c2) == 1, ".. a pass later: its early note");
        CHECK(in_step(1, 2, L + 1, 52, -1) == 0 && in_step(1, 2, L + 2, 52, -1) == 1,
              ".. one whose note plays late (in its own step): silent, then a pass later its note");
    }
    CHECK(in_pass(3, L + 2, -1) == 1 && in_step(3, 8, L + 2, 36, 70) == 1 && in_pass(3, L + 3, -1) == 11,
          "E6 a loop masked: only the hit scheduled in it (%u notes), a loop later the 11 of its own (%u)",
          in_pass(3, L + 2, -1), in_pass(3, L + 3, -1));
    for (i = 0; i < 5; i++)
        k2[i] = in_pass(2, L2 + 1 + i, 55);
    CHECK(k2[0] == 0 && k2[1] == 0 && k2[2] == 6 && k2[3] == 0 && k2[4] == 6,
          "E7 1/8T x 6: a bar masked, two loops silent; one loop masked, one (notes a pass: %u %u %u %u %u)", k2[0], k2[1],
          k2[2], k2[3], k2[4]);
    CHECK(in_step(0, 0, L + 3, 48, -1) == 1, "E8 a mask cancelled before it starts: the step plays");
    CHECK(in_pass(0, L + 4, 48) == 0 && in_pass(0, L + 5, 48) > 0, ".. cancelled while running: the loop silent to its end");
    CHECK(in_step(0, 0, L + 5, 48, -1) == 0 && in_step(0, 4, L + 5, 48, -1) == 3,
          "E9 a mask of 2 steps from a bar's beat: from the step that starts there");
    w = at_step(0, 0, 1, FEL_LATE_NEXT);
    {
        fel_action_t none = {FEL_DO_MASK, 0, 0, FEL_SPAN_STEPS, 0}, much = {FEL_DO_MASK, 0, 4097, FEL_SPAN_STEPS, 0},
                     unit = {FEL_DO_MASK, 0, 1, 3, 0};
        CHECK(c->schedule(&w, &none, 1) == 1 && c->schedule(&w, &much, 1) == 1 && c->schedule(&w, &unit, 1) == 1,
              "not masks: 0 steps, 4097, a unit that is none");
    }
}

/* ---- F: a SET for an engine */
static void test_engine_set(const fel_test_t *x)
{
    const fel_core_t *c = x->core;
    fel_when_t w;
    fel_action_t analog = {FEL_DO_SET, 0, FEL_TEST_CUT, 120, 0}, other = {FEL_DO_SET, 0, FEL_TEST_CUT, 30, 1},
                 left = {FEL_DO_SET, 0, FEL_TEST_CUT, 40, 0}, bad = {FEL_DO_SET, 0, FEL_TEST_CUT, 40, 99};
    c->boot();
    x->setup(FEL_TEST_KNOCK);                           /* (track 1: ANALOG, engine 0) */
    c->tempo(120);
    run(c, frames_ms(300), 0);
    c->play(drive_timeline + 0.1);
    run(c, frames_ms(300), 0);
    w = at_step(3, 0, 1, FEL_LATE_NEXT);
    c->schedule(&w, &analog, 1201);
    c->schedule(&w, &other, 1202);
    nev = 0;
    run(c, frames_ms(2500), 0);
    CHECK(find(FEL_EV_ACTION, 1201) && c->param_get(0, FEL_TEST_CUT) == 120, "a SET for ANALOG on an ANALOG track: applied");
    CHECK(find(FEL_EV_DROPPED, 1202) && !find(FEL_EV_ACTION, 1202), "one for another engine: dropped");
    w = at_step(3, 0, 1, FEL_LATE_NEXT);
    c->schedule(&w, &left, 1203);
    c->engine_set(0, 1);                                /* (the track leaves ANALOG before the step) */
    nev = 0;
    run(c, frames_ms(2500), 0);
    CHECK(find(FEL_EV_DROPPED, 1203) && !find(FEL_EV_ACTION, 1203), "one for the engine the track has since left: dropped");
    CHECK(c->schedule(&w, &bad, 1) == 1, "one for an engine that is none: not an action");
}

int main(void)
{
    test_steps(fel_test_0());
    test_knock(fel_test_1());
    test_targets(fel_test_2(), fel_test_3());
    test_notes(fel_test_4());
    test_mask(fel_test_5());
    test_engine_set(fel_test_6());
    return check_done("test_sched");
}
