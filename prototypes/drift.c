/* Internal clock, one instance: a 1/16 track (16 steps) and a 1/4 track (4 steps), both a 4/4 bar long. Ten minutes
 * at 120 BPM: where is each track against the other, and against exact musical time (88200 samples a bar)? */
#include "felucca_api.c"
#include <stdio.h>
static uint64_t done;
static void go(uint32_t n) { uint32_t k; for (k = 0; k < n; k += 128) { web_render(128); done += 128; } }
int main(int argc, char **argv)
{
    int bpm = argc > 1 ? atoi(argv[1]) : 120;
    uint32_t t, loops[2] = {0, 0}, last[2] = {0, 0};
    uint64_t start;
    web_nor_erase(); web_master(0); web_boot(); go(66150);
    for (t = 0; t < 4; t++) memset(trk[t].step, 0, sizeof trk[t].step);
    trk[0].p[P_SDIV] = 2; trk[0].p[P_SLEN] = 16;           /* 1/16 x 16 */
    trk[1].p[P_SDIV] = 0; trk[1].p[P_SLEN] = 4;            /* 1/4 x 4 */
    fel_global_set(G_BPM, bpm);
    fel_play(1); go(128); start = done - 128;
    while (done - start < 600ull * 44100) {
        go(128);
        for (t = 0; t < 2; t++) { if (trk[t].seq_idx < last[t]) loops[t]++; last[t] = trk[t].seq_idx; }
    }
    {
        double el = (double)(done - start), bar = 44100.0 * 60 / bpm * 4;
        double b0 = (loops[0] * 16 + trk[0].seq_idx + trk[0].seq_pos / (double)div_samples(2)) / 16;
        double b1 = (loops[1] * 4 + trk[1].seq_idx + trk[1].seq_pos / (double)div_samples(0)) / 4;
        printf("%3d BPM, 10 min: 1/16 = %u (exact %.2f), 1/4 = %u (exact %.2f)\n", bpm, div_samples(2), bar / 16, div_samples(0), bar / 4);
        printf("   bars: exact %.4f  1/16 track %.4f (%+.1f ms)  1/4 track %.4f (%+.1f ms)  between them %.1f ms\n",
               el / bar, b0, (b0 - el / bar) * bar / 44.1, b1, (b1 - el / bar) * bar / 44.1, (b0 - b1) * bar / 44.1);
    }
    return 0;
}
