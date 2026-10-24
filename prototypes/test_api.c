/* Drives felucca_api.c headless, as a game would: hold notes, move parameters from code, measure the audio. */
#include "felucca_api.c"
#include <stdio.h>
#include <math.h>

static float buf_l[44100 * 4];
static uint32_t render_ms(uint32_t ms, float *dst)
{
    uint32_t n = ms * 441u / 10u, k, i;
    for (k = 0; k < n; k += 128) {
        uint32_t m = n - k < 128 ? n - k : 128;
        web_render(m);
        if (dst) for (i = 0; i < m; i++) dst[k + i] = web_out_l()[i];
    }
    return n;
}
/* brightness: energy of the first difference over the energy (a high-pass share, 0..4) */
static double brightness(const float *x, uint32_t n)
{
    double e = 1e-12, d = 0;
    uint32_t i;
    for (i = 1; i < n; i++) { e += x[i] * x[i]; d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]); }
    return d / e;
}
/* spectral centroid in Hz: a naive DFT of 2048 samples, 1024 bins (Hann window) */
static double centroid(const float *x, uint32_t n)
{
    static double w[2048]; uint32_t i, b, N = 2048; double num = 0, den = 1e-12;
    if (n < N) return 0;
    for (i = 0; i < N; i++) w[i] = x[n - N + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1)));
    for (b = 1; b < N / 2; b++) {
        double re = 0, im = 0;
        for (i = 0; i < N; i++) { re += w[i] * cos(2 * M_PI * b * i / N); im -= w[i] * sin(2 * M_PI * b * i / N); }
        num += (b * 44100.0 / N) * sqrt(re * re + im * im); den += sqrt(re * re + im * im);
    }
    return num / den;
}
static double rms(const float *x, uint32_t n) { double e = 0; uint32_t i; for (i = 0; i < n; i++) e += x[i] * x[i]; return sqrt(e / n); }
/* onsets per second: 5 ms RMS frames rising through half the peak */
static double onsets(const float *x, uint32_t n)
{
    uint32_t f, fr = 220, nf = n / fr, c = 0, i; double peak = 0, prev = 0, r[2000];
    for (f = 0; f < nf; f++) { double e = 0; for (i = 0; i < fr; i++) e += x[f * fr + i] * x[f * fr + i]; r[f] = sqrt(e / fr); if (r[f] > peak) peak = r[f]; }
    for (f = 0; f < nf; f++) { if (r[f] > peak * 0.5 && prev <= peak * 0.5) c++; prev = r[f]; }
    return c / (n / 44100.0);
}

int main(void)
{
    uint32_t n; int e, cut, res, lo, hi, amode, arate, k;
    web_nor_erase(); web_master(700); web_boot();
    render_ms(1500, 0);                                         /* past the splash */

    e = fel_engine_find("ANALOG");
    fel_engine_set(0, e);
    render_ms(200, 0);
    printf("track 1 engine: %s (%d)\n", ENGINES[trk[0].engine]->name, trk[0].engine);
    printf("engine params:");
    for (k = P_E0; k <= P_E7; k++) printf(" E%d=%s", k - P_E0, fel_param_label(0, k));
    printf("\n");

    /* 1. the filter cutoff, from code, while a note is held */
    cut = fel_param_find(0, "CUT"); res = fel_param_find(0, "RES");
    fel_param_range(0, cut, &lo, &hi);
    printf("CUT is id %d, range %d..%d\n", cut, lo, hi);
    fel_param_set(0, res, 0);
    fel_param_set(0, P_SUS, 127); fel_param_set(0, P_ED_FLT, 0); fel_param_set(0, P_LD_FLT, 0);
    web_midi(0x90, 48, 110);
    for (k = 0; k <= 4; k++) {
        int v = lo + (hi - lo) * k / 4;
        fel_param_set(0, cut, v);
        render_ms(100, 0);                                       /* settle */
        n = render_ms(300, buf_l);
        printf("  CUT %4d -> spectral centroid %6.0f Hz  rms %.3f\n", fel_param_get(0, cut), centroid(buf_l, n), rms(buf_l, n));
    }
    web_midi(0x80, 48, 0);
    render_ms(500, 0);

    /* 2. the arpeggiator's rate, from code: no ARP page, no knob */
    amode = P_AMODE; arate = P_ARATE;
    fel_param_set(0, cut, hi);
    fel_param_set(0, P_REL, 0); fel_param_set(0, P_ATK, 0); fel_param_set(0, P_SUS, 0); fel_param_set(0, P_DEC, 20);
    fel_param_range(0, amode, &lo, &hi);
    printf("ARP mode %s %d..%d, rate %s ", fel_param_label(0, amode), lo, hi, fel_param_label(0, arate));
    fel_param_range(0, arate, &lo, &hi);
    printf("%d..%d\n", lo, hi);
    fel_param_set(0, amode, 1);
    web_midi(0x90, 48, 110); web_midi(0x90, 52, 110); web_midi(0x90, 55, 110);
    for (k = lo; k <= hi; k += (hi - lo) / 3 ? (hi - lo) / 3 : 1) {
        char val[24]; const char *unit = "";
        fel_param_set(0, arate, k);
        param_format(track_desc(&trk[0], arate), trk[0].p[arate], val, &unit);
        render_ms(300, 0);
        n = render_ms(2000, buf_l);
        printf("  ARP RATE %2d (%s%s) -> %.1f onsets/s\n", k, val, unit, onsets(buf_l, n));
    }
    web_midi(0x80, 48, 0); web_midi(0x80, 52, 0); web_midi(0x80, 55, 0);

    /* 3. a global: tempo */
    printf("BPM -> %d\n", fel_global_set(G_BPM, 140));

    /* 4. project bytes round trip (what the web editor exports), into the running instance */
    {
        static uint8_t proj[PROJ_STORE_SIZE];
        int len = fel_project_export(proj), before = trk[0].p[cut], r;
        fel_param_set(0, cut, 0);
        r = fel_project_import(proj, len);
        render_ms(100, 0);
        printf("project: %d bytes, magic %.4s; CUT %d -> 0 -> import %s -> %d\n", len, (char *)proj,
               before, r ? "FAILED" : "ok", trk[0].p[cut]);
    }
    return 0;
}
