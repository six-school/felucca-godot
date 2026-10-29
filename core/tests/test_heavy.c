/* The heavy test song (felucca_web.c web_test_heavy: test_cpu's and test_pool's) as a project file, for what
 * demo/tests/m7_bench measures in Godot (the CPU of 1 and 8 instances in a browser and on the desktop). Copy 6
 * makes the song and exports it; copy 7 imports the bytes; both play on the same timeline for 16 s and must sound
 * the same (the project holds the whole song: its presets, voices, chords and drums). The bytes go to the path given;
 * core/tests/run.sh wraps them as the web app's export does (demo/tests/heavy.felucca).
 *   test_heavy OUT.bin */
#include "fel_pool.h"
#include "drive.h"
#include "check.h"
#include <stdio.h>

void fel6_web_test_heavy(void);                         /* (renamed: copy 6's) */

static float la[44100 * 16], lb[44100 * 16];
static uint8_t proj[FEL_PROJECT_SIZE];

int main(int argc, char **argv)
{
    const fel_core_t *cs[2] = {fel_pool_core(6), fel_pool_core(7)};
    float *ls[2] = {la, lb};
    uint32_t n;
    FILE *f;
    if (argc < 2) {
        printf("test_heavy OUT.bin\n");
        return 2;
    }
    cs[0]->boot();
    cs[1]->boot();
    fel6_web_test_heavy();
    n = cs[0]->project_export(proj);
    CHECK(n == FEL_PROJECT_SIZE && !cs[1]->project_import(proj, n), "the heavy song exported (%u bytes), imported", n);
    drive_bpm = 120;
    cs[0]->tempo(drive_bpm);
    cs[1]->tempo(drive_bpm);
    cs[0]->play(drive_timeline);
    cs[1]->play(drive_timeline);
    drive(cs, 2, 16 * FEL_RATE, ls);
    printf("  16 s: rms %.3f made, %.3f imported\n", rms_of(la, 16 * FEL_RATE), rms_of(lb, 16 * FEL_RATE));
    CHECK(rms_of(la, 16 * FEL_RATE) > 0.02 && !memcmp(la, lb, sizeof la), "16 s, the same audio from the project");
    f = fopen(argv[1], "wb");
    CHECK(f && fwrite(proj, 1, n, f) == n && !fclose(f), "written to %s", argv[1]);
    return check_done("the heavy song as a project (core/tests/test_heavy.c)");
}
