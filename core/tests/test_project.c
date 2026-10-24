/* T3, T11 (docs/DESIGN.md §10) through fel_core_t, white-box (one copy compiled in):
 *   T3   a project's bytes out and back in: 3840 bytes, magic ANUF, values restored; damaged bytes refused; the clock
 *        and MIDI routing the project carries do not take the core's over (the transport plays on B after)
 *   T11  the user's own export (tests/fixtures): JSON checked (core/project_json.c), imported, what it holds, played
 *        16 s on B at its own tempo
 *   test_project tests/fixtures/Felucca-project-D-2026-10-24.json */
#include "board.c"
#include "project_json.h"
#include "drive.h"
#include "check.h"

static float audio[44100 * 16];
static uint8_t proj[FEL_PROJECT_SIZE], bad[FEL_PROJECT_SIZE + 4];
static char json[1 << 16];
static fel_project_file_t file_;

int main(int argc, char **argv)
{
    const fel_core_t *c = fel_core_0();
    const int CUT = P_E0 + 4;                           /* ANALOG's E4 */
    FILE *f;
    uint32_t n, k;
    c->boot();
    c->engine_set(0, 0);
    drive1(c, frames_ms(200), 0);

    /* ---- T3 ---- */
    c->param_set(0, CUT, 127);
    c->param_set(1, P_SLEN, 12);
    CHECK(c->project_export(proj) == FEL_PROJECT_SIZE && !memcmp(proj, "ANUF", 4), "T3: 3840 bytes out, magic ANUF");
    c->param_set(0, CUT, 0);
    c->param_set(1, P_SLEN, 16);
    c->play(drive_b);
    drive1(c, frames_ms(500), 0);
    song.g[G_CLOCK] = 0;                               /* (as a project made on an internal clock carries it: the */
    song.g[G_ROUTE] = 1;                               /* export below stores these) */
    CHECK(c->project_export(bad) == FEL_PROJECT_SIZE, "T3: a second export (CLK INT, ROUT SEL in it)");
    song.g[G_CLOCK] = 1;
    song.g[G_ROUTE] = 0;
    CHECK(c->project_import(proj, FEL_PROJECT_SIZE) == 0 && c->param_get(0, CUT) == 127 && c->param_get(1, P_SLEN) == 12,
          "T3: imported, CUT 127 and track 2's LEN 12 back");
    CHECK(!song.playing, "T3: an import stops the transport");
    CHECK(c->project_import(bad, FEL_PROJECT_SIZE) == 0 && c->global_get(G_CLOCK) == 1 && c->global_get(G_ROUTE) == 0,
          "T3: a project's CLK and ROUT do not reach the core (still USB clock, CH1-4)");
    c->play(drive_b);
    drive1(c, frames_ms(1000), 0);
    {
        fel_snapshot_t s;
        c->snapshot(&s);
        CHECK(s.playing && fabs(s.beats - 2.0) < 0.01, "T3: after the import it plays on B (%.4f beats in 1 s at 120)",
              s.beats);
    }
    c->param_set(0, CUT, 77);
    memcpy(bad, proj, sizeof proj);
    bad[2000] ^= 0x40;
    CHECK(c->project_import(bad, FEL_PROJECT_SIZE) != 0, "T3: one bit changed: refused (the store's hash)");
    memset(bad, 0, sizeof bad);
    CHECK(c->project_import(bad, FEL_PROJECT_SIZE) != 0 && c->project_import(proj, 100) != 0 &&
          c->project_import(bad, sizeof bad) != 0 && c->project_import(0, 0) != 0,
          "T3: zeros, 100 bytes, 3844 bytes, none: refused");
    CHECK(c->param_get(0, CUT) == 77, "T3: a refused import changes nothing");

    /* ---- T11: the user's export ---- */
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        printf("test_project FILE.json\n");
        return 2;
    }
    n = (uint32_t)fread(json, 1, sizeof json, f);
    fclose(f);
    k = (uint32_t)fel_project_json(json, n, &file_);
    CHECK(k == FEL_PJ_OK && file_.size == FEL_PROJECT_SIZE && file_.crc == 0x642C0757u && !strcmp(file_.firmware, "FELUCCA v1.6.1"),
          "T11: the file checks out (%s): %u bytes, CRC %08X, %s", fel_project_json_error((int)k), file_.size, file_.crc,
          file_.firmware);
    {   /* what the importer reads, without loading: the instance (CUT 77, the project before) stays as it is */
        fel_project_info_t info;
        int t, eng = 1;
        static const char *const ENG[4] = {"PHASE", "PHASE", "LOFI", "DRUM"};
        CHECK(c->project_info(file_.data, file_.size, &info) == 0 && !strcmp(info.name, "SOLAR SIGNAL") &&
              info.bpm == 68, "T11 info: \"%s\", %d BPM", info.name, info.bpm);
        for (t = 0; t < 4; t++)
            eng &= c->engine_name(info.engine[t]) && !strcmp(c->engine_name(info.engine[t]), ENG[t]);
        CHECK(eng && !c->engine_name(-1) && !c->engine_name(NENGINES) && !strcmp(c->engine_name(1), "-"),
              "T11 info: PHASE PHASE LOFI DRUM; engine names, the retired slot '-'");
        CHECK(c->param_get(0, CUT) == 77 && c->engine_get(0) == 0, "T11 info: the instance untouched");
        CHECK(c->project_info(bad, FEL_PROJECT_SIZE, &info) == 1 && c->project_info(file_.data, 100, &info) == 1,
              "T11 info: not a project: 1");
    }
    CHECK(c->project_import(file_.data, file_.size) == 0, "T11: imported");
    {
        static const char *const ENG[4] = {"PHASE", "PHASE", "LOFI", "DRUM"};
        char name[PROJ_NAME_LEN + 1];
        int t, eng = 1;
        project_cur_name(name);
        for (t = 0; t < 4; t++)
            eng &= !strcmp(ENGINES[c->engine_get(t)]->name, ENG[t]);
        CHECK(!strcmp(name, "SOLAR SIGNAL") && c->global_get(G_BPM) == 68, "T11: \"%s\" at %d BPM", name,
              c->global_get(G_BPM));
        CHECK(eng && c->param_get(0, P_SLEN) == 32 && c->param_get(3, P_SLEN) == 16,
              "T11: PHASE (LEN 32), PHASE, LOFI, DRUM (LEN 16)");
    }
    {
        double b0, lo = 1, pk;
        fel_snapshot_t s;
        drive_bpm = c->global_get(G_BPM);               /* the project's tempo, as the game may copy it */
        c->tempo(drive_bpm);
        b0 = drive_b;
        c->play(b0);
        drive1(c, 16 * FEL_RATE, audio);
        c->snapshot(&s);
        pk = peak_of(audio, 16 * FEL_RATE);
        printf("  T11 16 s at %.0f BPM: peak %.3f, rms per 2 s:", drive_bpm, pk);
        for (k = 0; k < 8; k++) {
            double r = rms_of(audio + k * 2 * FEL_RATE, 2 * FEL_RATE);
            printf(" %.3f", r);
            lo = r < lo ? r : lo;
        }
        printf("\n");
        CHECK(pk > 0.1 && pk < 1.0 && lo > 0.02, "T11: it plays throughout (peak %.3f, quietest 2 s %.3f)", pk, lo);
        /* where the music is: 16 s at 68 BPM = 18.13 beats = 72.5 sixteenths; track 4 (LEN 16) in its step 9 */
        CHECK(s.playing && fabs(s.beats - (drive_b - b0)) < 0.01 && s.track[3].step == 8,
              "T11: at %.4f beats (B moved %.4f), track 4 on step %u", s.beats, drive_b - b0, s.track[3].step + 1);
    }
    return check_done("T3 T11 projects (core/tests/test_project.c)");
}
