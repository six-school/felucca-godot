/* T8 (docs/DESIGN.md §10) on the pool as L2 gets it: libfelucca_core.a, every copy renamed (core/build.sh), only
 * fel_core_t in use (plus each copy's renamed test hook for the heavy song). Black-box: ids are the firmware's
 * numbers (prototypes/params.json, Felucca 1.6.1).
 *   isolation  copies 0 and 1, different values and notes: each hears only its own
 *   T11        the user's project in copies 2 and 3 on the same timeline: the same audio, sample for sample
 *   T8         the heavy song in copies 4 and 5 on the same timeline for 10
 *              minutes: the same positions and audio throughout
 * (each pair fresh and treated alike: an import does not reset an instance's FX tails or noise, as on the device)
 *   test_pool tests/fixtures/Felucca-project-D-2026-10-24.json */
#include "fel_pool.h"
#include "project_json.h"
#include "drive.h"
#include "check.h"
#include <stdlib.h>

enum { ANALOG = 0, ANALOG_CUT = 107 };                 /* engine 0; its E4 (P_E0 + 4) */
void fel4_web_test_heavy(void), fel5_web_test_heavy(void);   /* (renamed: copy 4's and copy 5's) */

static float la[44100 * 16], lb[44100 * 16];
static char json[1 << 16];
static fel_project_file_t file_;

int main(int argc, char **argv)
{
    const fel_core_t *cs[2];
    float *ls[2] = {la, lb};
    FILE *f;
    int i, distinct = 1;
    uint32_t n;
    CHECK(fel_pool_size() == 8 && !fel_pool_core(8) && !fel_pool_core(-1), "the pool: 8 copies");
    for (i = 0; i < fel_pool_size(); i++) {
        int j;
        for (j = 0; j < i; j++)
            distinct &= fel_pool_core(i) != fel_pool_core(j);
        distinct &= fel_pool_core(i) && fel_pool_core(i)->abi == FEL_CORE_ABI;
    }
    CHECK(distinct, "8 tables of their own, this ABI");
    /* ---- isolation ---- */
    cs[0] = fel_pool_core(0);
    cs[1] = fel_pool_core(1);
    cs[0]->boot();
    cs[1]->boot();
    cs[0]->engine_set(0, ANALOG);
    cs[1]->engine_set(0, ANALOG);
    drive(cs, 2, frames_ms(300), 0);
    cs[0]->param_set(0, ANALOG_CUT, 20);
    cs[1]->param_set(0, ANALOG_CUT, 127);
    CHECK(cs[0]->param_get(0, ANALOG_CUT) == 20 && cs[1]->param_get(0, ANALOG_CUT) == 127, "isolation: CUT 20 and 127");
    cs[0]->note(0, 48, 110);
    drive(cs, 2, frames_ms(400), ls);
    printf("  a note in copy 0 only: rms %.3f and %.6f\n", rms_of(la, frames_ms(400)), rms_of(lb, frames_ms(400)));
    CHECK(rms_of(la, frames_ms(400)) > 0.01 && rms_of(lb, frames_ms(400)) == 0.0, "isolation: copy 1 hears nothing");
    cs[1]->note(0, 48, 110);
    drive(cs, 2, frames_ms(400), ls);
    CHECK(rms_of(lb, frames_ms(400)) > 0.01 && memcmp(la, lb, frames_ms(400) * sizeof *la),
          "isolation: the same note in both, each with its own CUT (rms %.3f, %.3f)", rms_of(la, frames_ms(400)),
          rms_of(lb, frames_ms(400)));
    cs[0]->note(0, 48, 0);
    cs[1]->note(0, 48, 0);

    /* ---- T11 twice: the user's project in both, on the same timeline ---- */
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        printf("test_pool FILE.json\n");
        return 2;
    }
    n = (uint32_t)fread(json, 1, sizeof json, f);
    fclose(f);
    cs[0] = fel_pool_core(2);
    cs[1] = fel_pool_core(3);
    cs[0]->boot();
    cs[1]->boot();
    CHECK(fel_project_json(json, n, &file_) == FEL_PJ_OK && !cs[0]->project_import(file_.data, file_.size) &&
          !cs[1]->project_import(file_.data, file_.size), "T11: the user's project into both");
    drive_bpm = 68;
    cs[0]->tempo(drive_bpm);
    cs[1]->tempo(drive_bpm);
    cs[0]->play(drive_timeline);
    cs[1]->play(drive_timeline);
    drive(cs, 2, 16 * FEL_RATE, ls);
    CHECK(rms_of(la, 16 * FEL_RATE) > 0.02 && !memcmp(la, lb, sizeof la), "T11: 16 s, the same audio in both");

    /* ---- T8: the heavy song, 10 minutes, the same timeline ---- */
    {
        uint64_t blocks = 0, diff = 0, k, total = 600ull * FEL_RATE;
        fel_snapshot_t sa, sb;
        double b0;
        cs[0] = fel_pool_core(4);
        cs[1] = fel_pool_core(5);
        cs[0]->boot();
        cs[1]->boot();
        fel4_web_test_heavy();
        fel5_web_test_heavy();
        drive_bpm = 120;
        cs[0]->tempo(drive_bpm);
        cs[1]->tempo(drive_bpm);
        b0 = drive_timeline + 0.5;                      /* (half a beat ahead: both start inside the same block) */
        cs[0]->play(b0);
        cs[1]->play(b0);
        for (k = 0; k < total; k += 512, blocks++) {
            drive(cs, 2, 512, ls);
            diff += memcmp(la, lb, 512 * sizeof *la) != 0;
        }
        cs[0]->snapshot(&sa);
        cs[1]->snapshot(&sb);
        printf("  T8 10 min: copy 0 at %.4f beats, step %u + %u; copy 1 at %.4f, step %u + %u; %llu of %llu mix steps"
               " differ\n", sa.beats, sa.track[0].step, sa.track[0].pos, sb.beats, sb.track[0].step, sb.track[0].pos,
               (unsigned long long)diff, (unsigned long long)blocks);
        CHECK(snap_same(&sa, &sb) && sa.playing && sa.beats > drive_timeline - b0 - 1.0 / 24000 &&
              sa.beats < drive_timeline - b0 + 128.0 * 2 / FEL_RATE, "T8: the same positions after 10 min, on the"
              " timeline (%.4f beats, it moved %.4f: within a clock unit behind, one half ahead)", sa.beats,
              drive_timeline - b0);
        CHECK(diff == 0, "T8: the same audio throughout");
    }
    return check_done("T8 pool (core/tests/test_pool.c)");
}
