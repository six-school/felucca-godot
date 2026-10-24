/* Tempo tweened from the game side, three ways, against an exact musical timeline.
 *   MODE 1 (A, today's internal clock): the conductor writes G_BPM.
 *   MODE 2 (B, option 4): PR 269's external clock; the conductor sends 24-ppqn clock pulses at their exact sample
 *          times; each is stamped with the device millisecond it arrives in (midi_enqueue), as on the hardware.
 *   MODE 3 (C, host clock): PR 269's grid fed directly by the conductor (FELUCCA_HOST_CLOCK): exact musical units per
 *          32-sample block, and the beat length set when the tempo changes.
 * Three tracks, each a 4/4 bar: 1/16 x 16, 1/4 x 4, 1/8T x 12. A step's onset is the start of the 32-sample block in
 * which the firmware entered it (the FEL_MEASURE hook, after seq_tick): when its notes start. */
#define FEL_MEASURE 1
#if MODE == 3
#define FELUCCA_HOST_CLOCK 1
#endif
#include "felucca_api.c"
#include <stdio.h>
#include <math.h>

/* ---- the tempo program: what a Godot tween of an int property does (a new integer each time it changes) ---- */
static double prog_bpm(double s)                   /* s: seconds since PLAY */
{
    double c = fmod(s, 34.0);
    if (c < 8) return 120;                                      /* hold */
    if (c < 10) return floor(120 + (c - 8) / 2 * 20);          /* 120 -> 140 over 2 s */
    if (c < 16) return 140;
    if (c < 20) return ceil(140 - (c - 16) / 4 * 40);          /* 140 -> 100 over 4 s */
    if (c < 26) return 100;
    if (c < 28) return floor(100 + (c - 26) / 2 * 20);         /* 100 -> 120 over 2 s */
    return 120;
}

/* ---- the conductor's exact timeline: beats since PLAY, the tempo constant within each 128-sample half ---- */
#define MAXH (44100 * 320 / 128)
static double hbeat[MAXH], hbpm[MAXH];             /* at the start of half h after PLAY */
static uint64_t s0;                                /* the device sample PLAY starts on (a half's start) */
static int playing_glue;
static double exact_beat(uint64_t s)
{
    uint64_t d = s - s0, h = d / 128;
    return hbeat[h] + (double)(d - h * 128) * hbpm[h] / (60.0 * FS);
}
static double bpm_at(uint64_t s) { return hbpm[(s - s0) / 128]; }

#if MODE == 2
static uint64_t pulse_k;
static uint64_t pulse_sample(uint64_t k);
#endif
/* ---- measurement: every step entered, against where the exact timeline says it belongs ---- */
static const double SPB[3] = {0.25, 1.0, 1.0 / 3};             /* beats a step */
static uint64_t dev;                                             /* device samples: the start of the block events_block runs for */
static uint16_t last_idx[3] = {0xFFFF, 0xFFFF, 0xFFFF};
static uint32_t steps[3];
static double err_max[3], err_sq[3], hold_max[3], hold_sq[3];
static uint32_t err_n[3], hold_n[3];
static uint32_t beat_changes, hold_beat_changes, last_beat_len;
static void fel_measure_block(uint32_t n)
{
    uint32_t t;
    if (playing_glue && song.playing && dev >= s0) {
        double s = (double)(dev - s0) / FS, c = fmod(s, 34.0);
        int hold = c < 8 || (c >= 10 && c < 16) || (c >= 20 && c < 26) || c >= 28;
        for (t = 0; t < 3; t++)
            if (trk[t].seq_pos < 0x7FFFFFFFu && trk[t].seq_idx != last_idx[t]) {
                if (last_idx[t] != 0xFFFF) steps[t]++;
                last_idx[t] = trk[t].seq_idx;
                {
                    double e = (exact_beat(dev) - steps[t] * SPB[t]) * 60.0 * 1000 / bpm_at(dev);   /* ms, + = late */
                    if (fabs(e) > err_max[t]) err_max[t] = fabs(e);
                    err_sq[t] += e * e; err_n[t]++;
                    if (hold) { if (fabs(e) > hold_max[t]) hold_max[t] = fabs(e); hold_sq[t] += e * e; hold_n[t]++; }
                }
            }
        if (beat_samples() != last_beat_len) {           /* the delay's time (and the synced LFOs, ENV SYNC) moves */
            if (last_beat_len) { beat_changes++; if (hold) hold_beat_changes++; }
            last_beat_len = beat_samples();
        }
    }
#if MODE == 2
    if (playing_glue) {                                       /* the clocks due before the next block: it reads them */
        while (pulse_sample(pulse_k) < dev + n) { midi_enqueue(0xFu | 0xF8u << 8, 1); pulse_k++; }
    }
#endif
    dev += n;
}

#if MODE == 3
/* ---- the host clock: exact musical units for each block, from the conductor's timeline ---- */
static double hc_beats; static uint64_t hc_units; static double hc_bpm;
static uint32_t host_clock_units(uint32_t n)
{
    uint64_t total;
    hc_beats += n * hc_bpm / (60.0 * FS);
    total = (uint64_t)floor(hc_beats * MIDI_CLOCK_GRID + 1e-9);
    n = (uint32_t)(total - hc_units);
    hc_units = total;
    return n;
}
static void hc_tempo(double bpm)
{
    uint32_t nb = (uint32_t)lround(60.0 * FS / bpm);
    hc_bpm = bpm;
    midi_clock_retempo(beat_samples(), nb);
    song.g[G_BPM] = (int16_t)lround(bpm);                         /* (the screen) */
}
#endif

#if MODE == 2
static uint64_t pulse_sample(uint64_t k)                         /* when the exact timeline reaches k / 24 */
{
    double b = k / 24.0;
    uint64_t lo = 0, hi = MAXH - 1;
    while (lo < hi) { uint64_t m = (lo + hi + 1) / 2; if (hbeat[m] <= b) lo = m; else hi = m - 1; }
    return s0 + lo * 128 + (uint64_t)ceil((b - hbeat[lo]) * 60.0 * FS / hbpm[lo] - 1e-9);
}
#endif

int main(void)
{
    uint64_t rendered = 0, end;
    uint32_t h, t, last_bpm = 0;
    web_nor_erase(); web_master(0); web_boot();
    for (t = 0; t < 4; t++) memset(trk[t].step, 0, sizeof trk[t].step);
    trk[0].p[P_SDIV] = 2; trk[0].p[P_SLEN] = 16;                  /* 1/16 */
    trk[1].p[P_SDIV] = 0; trk[1].p[P_SLEN] = 4;                   /* 1/4 */
    trk[2].p[P_SDIV] = 4; trk[2].p[P_SLEN] = 12;                  /* 1/8T */
    for (h = 0; h < MAXH; h++) {                                  /* the program, as the conductor will play it */
        hbpm[h] = prog_bpm(h * 128.0 / FS);
        hbeat[h] = h ? hbeat[h - 1] + 128 * hbpm[h - 1] / (60.0 * FS) : 0;
    }
#if MODE != 1
    song.g[G_CLOCK] = 1;                                          /* external clock (USB): PR 269's grid */
#endif
    while (rendered < 66150) {                                    /* boot (and let the clock mode settle) */
        int isr = web_samples_due + FS >= 1000u * HALF_FRAMES;
        web_ms(); if (isr) rendered += HALF_FRAMES;
    }
    end = rendered + 300ull * FS;
    while (rendered < end) {
        int isr = web_samples_due + FS >= 1000u * HALF_FRAMES;
        if (isr && !playing_glue) {                               /* PLAY, on a half's start */
            s0 = rendered; playing_glue = 1;
#if MODE == 1
            fel_global_set(G_BPM, 120); fel_play(1);
#elif MODE == 2
            midi_enqueue(0xFu | 0xFAu << 8, 1);                   /* Start, then the first clock with it */
            midi_enqueue(0xFu | 0xF8u << 8, 1); pulse_k = 1;
#else
            host_clock_on = 1; hc_tempo(120); midi_clock_transport(0xFAu, fm1_ms);
#endif
        }
        if (isr && playing_glue) {                                /* the conductor, before each half */
            uint32_t bpm = (uint32_t)hbpm[(rendered - s0) / 128];
            if (bpm != last_bpm) {
#if MODE == 1
                fel_global_set(G_BPM, (int)bpm);
#elif MODE == 3
                hc_tempo(bpm);
#endif
                last_bpm = bpm;
            }
        }
        web_ms(); if (isr) rendered += HALF_FRAMES;
    }
    {
        static const char *NAME[3] = {"1/16", "1/4 ", "1/8T"};
        printf("%s\n", MODE == 1 ? "A  internal clock, G_BPM tweened (today)" :
                       MODE == 2 ? "B  option 4: PR 269 + 24-ppqn pulses (ms timestamps)" :
                                   "C  host clock: PR 269's grid fed by the conductor");
        for (t = 0; t < 3; t++)
            printf("   %s track: %5u steps  error max %7.2f ms  rms %6.2f ms   | in the holds: max %6.2f ms  rms %5.2f ms\n",
                   NAME[t], err_n[t], err_max[t], sqrt(err_sq[t] / err_n[t]), hold_max[t], sqrt(hold_sq[t] / hold_n[t]));
        printf("   beat length changed %u times (%u of them during the holds, where the tempo is constant)\n",
               beat_changes, hold_beat_changes);
    }
    return 0;
}
