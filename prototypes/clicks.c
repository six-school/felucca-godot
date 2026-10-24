/* A held sine through the delay, 20 s: does the delay's time move, and does it click? MODE 2 (B) or 3 (C); TWEEN 1:
 * 128 BPM, then 120 -> 140 over 2 s (integer steps), else a constant 128 BPM. */
#if MODE == 3
#define FELUCCA_HOST_CLOCK 1
#endif
#define FEL_MEASURE 1
#include "felucca_api.c"
#include <stdio.h>
#include <math.h>
static uint64_t dev, s0; static int on;
static double bpm_now(uint64_t s) { double t = (double)(s - s0) / FS; return !TWEEN || t < 6 ? 128 : t < 8 ? floor(120 + (t - 6) * 10) : 140; }
static double beats;                                             /* the conductor's exact timeline */
#if MODE == 3
static uint64_t hc_units;
static uint32_t host_clock_units(uint32_t n)
{ uint64_t total; beats += n * bpm_now(dev) / (60.0 * FS); total = (uint64_t)floor(beats * MIDI_CLOCK_GRID + 1e-9);
  n = (uint32_t)(total - hc_units); hc_units = total; return n; }
#endif
static uint64_t pulses = 0; static double pb;                    /* MODE 2: the pulses' own timeline */
static void blockhook(uint32_t n)
{
#if MODE == 2
    if (on) { uint64_t s; for (s = dev; s < dev + n; s++) { pb += bpm_now(s) / (60.0 * FS); while (pb * 24 >= pulses + 1) { midi_enqueue(0xFu | 0xF8u << 8, 1); pulses++; } } }
#endif
    dev += n;
}
static void fel_measure_block(uint32_t n) { blockhook(n); }
static float out[44100 * 21];
static uint64_t chg_at[4096]; static uint32_t nchg;
int main(int argc, char **argv)
{
    uint64_t rendered = 0; uint32_t k, n = 0, clicks = 0, changes = 0, lastb = 0, last_bpm = 0, len[2] = {~0u, 0};
    web_nor_erase(); web_master(700); web_boot();
    for (k = 0; k < 4; k++) memset(trk[k].step, 0, sizeof trk[k].step);
    fel_engine_set(0, fel_engine_find("ANALOG"));
    song.g[G_CLOCK] = 1;
    while (rendered < 66150) { int isr = web_samples_due + FS >= 1000u * HALF_FRAMES; web_ms(); if (isr) rendered += 128; }
    fel_param_set(0, P_E0, 3);                                  /* ANALOG WAVE (E0): SIN; "WAVE" by label is the LFO's */
    fel_param_set(0, fel_param_find(0, "CUT"), 127);
    fel_param_set(0, P_SUS, 127); fel_param_set(0, P_DLY, 127); fel_param_set(0, P_REV, 0); fel_param_set(0, P_CHOR, 0);
    fel_global_set(G_DTIME, 0); fel_global_set(G_DFDBK, 60); fel_global_set(G_DMIX, argc > 1 ? atoi(argv[1]) : 127);   /* a 1/4 delay */
    s0 = rendered; on = 1;
#if MODE == 2
    midi_enqueue(0xFu | 0xFAu << 8, 1); midi_enqueue(0xFu | 0xF8u << 8, 1); pulses = 1;
#else
    host_clock_on = 1; midi_clock_retempo(beat_samples(), (uint32_t)lround(60.0 * FS / 128)); midi_clock_transport(0xFAu, fm1_ms);
#endif
    web_midi(0x90, 57, 100);                                    /* A3, held */
    while (rendered < s0 + 20ull * FS) {
        int isr = web_samples_due + FS >= 1000u * HALF_FRAMES;
#if MODE == 3
        if (isr) { uint32_t b = (uint32_t)bpm_now(rendered); if (b != last_bpm) { midi_clock_retempo(beat_samples(), (uint32_t)lround(60.0 * FS / b)); last_bpm = b; } }
#endif
        web_ms();
        if (isr) {
            rendered += 128;
            if (beat_samples() != lastb) { if (lastb && rendered > s0 + 3ull * FS) { changes++; if (nchg < 4096) chg_at[nchg++] = rendered; } lastb = beat_samples(); }
            if (beat_samples() < len[0]) len[0] = beat_samples(); if (beat_samples() > len[1]) len[1] = beat_samples();
        }
        while (ring_r != ring_w) { uint32_t i = (ring_r++ % RING) * 2u; if (n < 44100 * 21) out[n++] = ring[i]; }
    }
    if (argc > 2) {                                       /* the audio and the change times, for the analysis */
        FILE *f = fopen(argv[2], "wb"); fwrite(&n, 4, 1, f); fwrite(out, 4, n, f); fwrite(&nchg, 4, 1, f);
        for (k = 0; k < nchg; k++) { uint32_t c = (uint32_t)chg_at[k]; fwrite(&c, 4, 1, f); } fclose(f);
    }
    return 0;
}
