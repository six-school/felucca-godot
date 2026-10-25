/* The cut (fel_core.h, DESIGN.md §7, §13.9) on test copies (core/tests/test_hooks.c) sharing one timeline, all rendering
 * the same chunks from boot (so their blocks fall alike). FEL_TEST_TAILS rings on after its notes: PHYS SYMP (strings
 * of up to 11 s) through a full-feedback INSERT flanger and a SLICER STUT, an ANALOG pad of the longest release, both
 * into the chorus, a delay fed back past 100 % and the longest HALL.
 *   C1  copy 1 cut while its notes ring, then given the user's project: exact zeros (both channels) for 10 s; copy 0,
 *       given the project the way a copy was reused before (an import alone: the notes released), still rings
 *   C2  then copy 1 and a fresh copy 2, both playing the project from one origin: the same positions every chunk, the
 *       first sound in the same frame, the level close (not the same samples: noise seeds and phases carry over)
 *   C3  copy 1 reused 100 times (import, play, 1 s, cut): silent after every cut, sounding and on time in every use
 *   test_cut tests/fixtures/Felucca-project-D-2026-10-24.json */
#include "test_hooks.h"
#include "project_json.h"
#include "check.h"
#include <math.h>
#include <stdlib.h>

const fel_test_t *fel_test_0(void), *fel_test_1(void), *fel_test_2(void);

#define NC 3
#define MIX 512u
static double timeline, bpm = 120.0;
static float L[NC][MIX], R[NC][MIX];
typedef struct { double e; uint64_t n, first; float peak; } stat_t;   /* first: the frame of the first sample != 0 */
static stat_t st[NC];
static uint64_t frame;

static void stat_reset(void)
{
    int i;
    for (i = 0; i < NC; i++)
        st[i] = (stat_t){0, 0, UINT64_MAX, 0.0f};
}

/* frames of copies cs[0 .. k-1] in mix steps, as Godot asks for them; both channels into the stats */
static void render(const fel_core_t *const *cs, int k, uint32_t frames)
{
    uint32_t done, m, j;
    int i;
    for (done = 0; done < frames; done += m) {
        double b1;
        m = frames - done < MIX ? frames - done : MIX;
        b1 = timeline + m * bpm / (60.0 * FEL_RATE);
        for (i = 0; i < k; i++) {
            cs[i]->render(m, timeline, b1, L[i], R[i]);
            for (j = 0; j < m; j++) {
                float a = fabsf(L[i][j]) > fabsf(R[i][j]) ? fabsf(L[i][j]) : fabsf(R[i][j]);
                st[i].e += (double)L[i][j] * L[i][j] + (double)R[i][j] * R[i][j];
                if (a > st[i].peak)
                    st[i].peak = a;
                if (a != 0.0f && st[i].first == UINT64_MAX)
                    st[i].first = frame + j;
            }
            st[i].n += 2u * m;
        }
        timeline = b1;
        frame += m;
    }
}
/* two snapshots alike, field by field (the struct has padding) */
static int snap_same_all(const fel_snapshot_t *a, const fel_snapshot_t *b)
{
    int t, same = a->playing == b->playing && a->beats == b->beats && a->frames == b->frames;
    for (t = 0; t < FEL_TRACKS; t++)
        same &= a->track[t].step == b->track[t].step && a->track[t].pos == b->track[t].pos &&
                a->track[t].loop == b->track[t].loop;
    return same;
}
static double rms(int i) { return st[i].n ? sqrt(st[i].e / st[i].n) : 0.0; }
static uint32_t ms(uint32_t t) { return t * (FEL_RATE / 1000u); }

static char json[1 << 16];
static fel_project_file_t file_;

int main(int argc, char **argv)
{
    const fel_test_t *x[NC] = {fel_test_0(), fel_test_1(), fel_test_2()};
    const fel_core_t *cs[NC], *c1;
    FILE *f;
    size_t n;
    int i, k, same = 1, cycles_quiet = 0, cycles_sound = 0, cycles_on_time = 0;
    double origin, worst_db = 0, lo = 1e9;
    fel_snapshot_t sa, sb;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        printf("usage: test_cut <a web app export .json>\n");
        return 2;
    }
    n = fread(json, 1, sizeof json, f);
    fclose(f);
    CHECK(fel_project_json(json, n, &file_) == FEL_PJ_OK, "the fixture reads");
    for (i = 0; i < NC; i++) {
        cs[i] = x[i]->core;
        cs[i]->boot();
        cs[i]->tempo(bpm);
    }
    c1 = cs[1];
    CHECK(c1->abi == 5, "ABI 5 (cut)");
    x[0]->setup(FEL_TEST_TAILS);
    x[1]->setup(FEL_TEST_TAILS);
    render(cs, NC, ms(300));
    for (i = 0; i < 2; i++) {                           /* chords on both parts, 1.5 s, released */
        static const int NOTES[2][2] = {{48, 55}, {60, 64}};
        for (k = 0; k < 4; k++)
            cs[i]->note(k / 2, NOTES[k / 2][k % 2], 120);
    }
    render(cs, NC, ms(1500));
    for (i = 0; i < 2; i++)
        for (k = 0; k < 4; k++)
            cs[i]->note(k / 2, k / 2 ? 60 + 4 * (k % 2) : 48 + 7 * (k % 2), 0);
    stat_reset();
    render(cs, NC, ms(300));
    printf("  released 0.3 s ago: rms %.4f and %.4f (copy 2, untouched: %.4f)\n", rms(0), rms(1), rms(2));
    CHECK(rms(0) > 0.01 && rms(1) > 0.01 && rms(2) == 0.0, "the tails ring before the cut (rms %.4f, %.4f)", rms(0),
          rms(1));

    /* ---- C1 ---- */
    CHECK(cs[0]->project_import(file_.data, file_.size) == 0, "C1: copy 0 takes the project (an import alone)");
    c1->cut();
    CHECK(c1->project_import(file_.data, file_.size) == 0, "C1: copy 1, cut, takes the project");
    CHECK(cs[2]->project_import(file_.data, file_.size) == 0, "C1: fresh copy 2 takes the project");
    stat_reset();
    render(cs, NC, ms(1000));
    printf("  C1 the first second after: import alone rms %.4f peak %.4f; cut first: peak %g; fresh: peak %g\n", rms(0),
           st[0].peak, st[1].peak, st[2].peak);
    CHECK(rms(0) > 0.001, "C1: an import alone leaves the tails ringing (rms %.4f): the check can see one", rms(0));
    CHECK(st[1].peak == 0.0f, "C1: cut, then imported: exact zeros in the first second (peak %g)", st[1].peak);
    stat_reset();
    render(cs, NC, ms(9000));
    CHECK(st[1].peak == 0.0f && st[2].peak == 0.0f, "C1: .. and for 9 s more (peak %g; fresh %g)", st[1].peak,
          st[2].peak);
    printf("  C1 9 s on: the import alone still at rms %.5f\n", rms(0));

    /* ---- C2 ---- */
    origin = timeline + 0.25 + 0.0137;                  /* (a quarter ahead, inside some block) */
    c1->play(origin);
    cs[2]->play(origin);
    for (k = 0; k < 8; k++) {                           /* 16 s in windows of 2 s */
        double db;
        uint32_t c;
        stat_t a, b;
        stat_reset();
        for (c = 0; c < ms(2000) / 32u; c++) {
            render(cs + 1, 2, 32u);
            c1->snapshot(&sa);
            cs[2]->snapshot(&sb);
            same &= snap_same_all(&sa, &sb);
        }
        a = st[0];                                      /* (render(cs + 1, ..): the stats of copies 1, 2 in 0, 1) */
        b = st[1];
        db = 10.0 * log10((a.e + 1e-30) / (b.e + 1e-30));
        if (fabs(db) > fabs(worst_db))
            worst_db = db;
        if (k == 0)
            CHECK(a.first == b.first && a.first != UINT64_MAX, "C2: the first sound in the same frame (%llu, %llu)",
                  (unsigned long long)a.first, (unsigned long long)b.first);
        printf("  C2 %2d..%2d s: rms %.4f / fresh %.4f (%+.2f dB)\n", 2 * k, 2 * k + 2, sqrt(a.e / a.n),
               sqrt(b.e / b.n), db);
    }
    CHECK(same, "C2: the cut copy and the fresh one at the same positions in every 32-frame chunk for 16 s");
    CHECK(fabs(worst_db) < 1.0, "C2: the same level within 1 dB in every 2 s (worst %+.2f dB)", worst_db);

    /* ---- C3 ---- */
    for (k = 0; k < 100; k++) {
        c1->cut();
        stat_reset();
        render(&c1, 1, ms(50));
        cycles_quiet += st[0].peak == 0.0f;
        CHECK(c1->project_import(file_.data, file_.size) == 0, "C3: the import of use %d", k);
        origin = timeline + 0.01;
        c1->play(origin);
        stat_reset();
        render(&c1, 1, ms(1000));
        c1->snapshot(&sa);
        cycles_sound += rms(0) > 0.001;
        lo = rms(0) < lo ? rms(0) : lo;
        /* (as rendered: the snapshot is up to a half of 128 frames ahead; a clock unit behind) */
        cycles_on_time += sa.playing && fabs(sa.beats - (timeline - origin)) < 128.0 * 2 / FEL_RATE + 1e-4;
    }
    CHECK(cycles_quiet == 100, "C3: exact zeros after every cut (%d of 100)", cycles_quiet);
    CHECK(cycles_sound == 100, "C3: it sounds in every use (%d of 100, the quietest second rms %.4f)", cycles_sound, lo);
    CHECK(cycles_on_time == 100, "C3: on time in every use (%d of 100)", cycles_on_time);
    return check_done("test_cut");
}
