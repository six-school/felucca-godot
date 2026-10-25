/* T1, T2 (docs/DESIGN.md §10) through fel_core_t: parameters set directly from code take effect live, never through
 * the emulated UI. White-box: one copy compiled in (board.c), the firmware's own ids and state for the checks.
 *   T1  ANALOG's CUT while a note is held: the sound brightens with it, CUT 0 near-silent
 *   T2  ARP RATE: every value plays at its note length, with the transport stopped and playing, at the tempo of the
 *      conductor's timeline
 * plus the API's edges: clamping, ids that do not exist, the globals the core keeps for itself. */
#include "board.c"
#include "drive.h"
#include "check.h"

static float buf[44100 * 2];

/* spectral centroid in Hz of the last 2048 samples (Hann window, a plain DFT) */
static double centroid(const float *x, uint32_t n)
{
    static double w[2048];
    uint32_t i, b, N = 2048;
    double num = 0, den = 1e-12;
    for (i = 0; i < N; i++)
        w[i] = x[n - N + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1)));
    for (b = 1; b < N / 2; b++) {
        double re = 0, im = 0, m;
        for (i = 0; i < N; i++) {
            re += w[i] * cos(2 * M_PI * b * i / N);
            im -= w[i] * sin(2 * M_PI * b * i / N);
        }
        m = sqrt(re * re + im * im);
        num += b * 44100.0 / N * m;
        den += m;
    }
    return num / den;
}

int main(void)
{
    const fel_core_t *c = fel_core_0();
    const int CUT = P_E0 + 4, RES = P_E0 + 5;       /* ANALOG's E4, E5 (by id: labels repeat across pages) */
    int k;
    CHECK(c->abi == FEL_CORE_ABI, "the table's ABI");
    c->boot();
    drive1(c, frames_ms(200), 0);

    /* ---- the API's edges ---- */
    CHECK(c->engine_set(0, 0) == 0 && c->engine_get(0) == 0 && !strcmp(ENGINES[0]->name, "ANALOG") &&
          !strcmp(ENGINES[0]->edit[4].label, "CUT") && !strcmp(ENGINES[0]->edit[5].label, "RES"),
          "track 1 is ANALOG, E4 its CUT, E5 its RES");
    CHECK(c->engine_set(0, NENGINES) == FEL_INVALID && c->engine_set(4, 0) == FEL_INVALID && c->engine_get(-1) == FEL_INVALID,
          "engines and tracks that do not exist");
    drive1(c, frames_ms(200), 0);                   /* (the engine switch fades) */
    CHECK(c->param_set(0, CUT, 500) == 127 && c->param_set(0, CUT, -5) == 0 && c->param_get(0, CUT) == 0,
          "CUT clamped to its descriptor (0..127)");
    CHECK(c->param_set(4, CUT, 1) == FEL_INVALID && c->param_set(0, P_COUNT, 1) == FEL_INVALID &&
          c->param_get(0, -1) == FEL_INVALID, "tracks and ids that do not exist");
    CHECK(c->global_set(G_DMIX, 50) == 50 && c->global_get(G_DMIX) == 50, "a sound global: the delay's MIX");
    CHECK(c->global_set(G_BPM, 90) == FEL_INVALID && c->global_set(G_CLOCK, 0) == FEL_INVALID &&
          c->global_set(G_ROUTE, 1) == FEL_INVALID && c->global_set(G_LOAD, 1) == FEL_INVALID &&
          c->global_get(G_CLOCK) == 1 && c->global_get(G_BPM) == 120, "BPM CLK ROUT LOAD: not writable (the core's)");

    /* ---- T1: CUT, live, a note held ---- */
    {
        double cen[5], lev[5];
        c->param_set(0, RES, 0);
        c->param_set(0, P_SUS, 127);
        c->param_set(0, P_ED_FLT, 0);
        c->param_set(0, P_LD_FLT, 0);
        c->note(0, 48, 110);
        for (k = 0; k <= 4; k++) {
            int v = 127 * k / 4;
            CHECK(c->param_set(0, CUT, v) == v, "CUT %d taken", v);
            drive1(c, frames_ms(100), 0);           /* (settle) */
            drive1(c, frames_ms(300), buf);
            cen[k] = centroid(buf, frames_ms(300));
            lev[k] = rms_of(buf, frames_ms(300));
            printf("  T1 CUT %3d: spectral centroid %5.0f Hz, rms %.3f\n", v, cen[k], lev[k]);
        }
        CHECK(cen[1] < cen[2] && cen[2] < cen[3] && cen[3] < cen[4] && cen[4] > 2 * cen[1],
              "T1: brighter with each CUT step from 31 to 127");
        CHECK(lev[0] < 0.1 * lev[4] && lev[4] > 0.05, "T1: CUT 0 near-silent (rms %.4f against %.3f)", lev[0], lev[4]);
        c->note(0, 48, 0);
        drive1(c, frames_ms(500), 0);
    }

    /* ---- T2: ARP RATE, every value: the arp's steps (arp_idx) vs its note length at the timeline's tempo ---- */
    {
        static const char *const NAME[10] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
        static const double BEATS[10] = {1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6, 2, 4, 8, 16};
        const param_desc_t *d = &TP[P_ARATE];
        int names = d->fmt == F_ENUM && d->min == 0 && d->max == 9;
        for (k = 0; names && k < 10; k++)
            names = !strcmp(d->names[k], NAME[k]);
        CHECK(names, "ARP RATE's values are 1/4 1/8 1/16 1/32 8T 16T 1/2 1/1 2BAR 4BAR (not by speed: §11)");
        c->param_set(0, CUT, 127);
        c->param_set(0, P_REL, 0); c->param_set(0, P_ATK, 0); c->param_set(0, P_SUS, 0); c->param_set(0, P_DEC, 20);
        CHECK(c->param_set(0, P_AMODE, 1) == 1, "ARP MODE UP");
        c->note(0, 48, 110); c->note(0, 52, 110); c->note(0, 55, 110);
        for (k = 0; names && k < 10; k++) {
            double sec = BEATS[k] * 4 * 60 / drive_bpm > 8 ? BEATS[k] * 4 * 60 / drive_bpm : 8, want;
            uint32_t i0, got;
            c->param_set(0, P_ARATE, k);
            drive1(c, frames_ms(300), 0);
            i0 = trk[0].arp_idx;
            drive1(c, (uint32_t)(sec * FEL_RATE), 0);
            got = trk[0].arp_idx - i0;
            want = sec * drive_bpm / 60 / BEATS[k];
            printf("  T2 ARP RATE %-4s (stopped): %3u steps in %4.1f s, %5.1f expected\n", NAME[k], got, sec, want);
            CHECK(fabs(got - want) <= 1.0, "T2: ARP RATE %s, stopped: %u steps, %.1f expected", NAME[k], got, want);
        }
        /* playing (an empty pattern), then at another tempo: the timeline's */
        c->param_set(0, P_ARATE, 2);
        c->play(drive_timeline);
        for (k = 0; k < 2; k++) {
            uint32_t i0, got;
            double want;
            if (k) { drive_bpm = 90; c->tempo(90); }
            drive1(c, frames_ms(300), 0);
            i0 = trk[0].arp_idx;
            drive1(c, 8 * FEL_RATE, 0);
            got = trk[0].arp_idx - i0;
            want = 8 * drive_bpm / 60 / 0.25;
            printf("  T2 ARP RATE 1/16 (playing, %3.0f BPM): %3u steps in 8 s, %5.1f expected\n", drive_bpm, got, want);
            CHECK(song.playing && fabs(got - want) <= 1.0, "T2: 1/16 playing at %.0f BPM: %u steps, %.1f expected",
                  drive_bpm, got, want);
        }
        CHECK(c->global_get(G_BPM) == 90, "tempo(90) shows as BPM 90");
        drive1(c, frames_ms(1000), buf);
        CHECK(rms_of(buf, frames_ms(1000)) > 0.01, "T2: the arp sounds");
    }
    return check_done("T1 T2 parameters (core/tests/test_params.c)");
}
