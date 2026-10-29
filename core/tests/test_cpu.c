/* The cost of one instance playing the heavy test song (felucca_web.c web_test_heavy: FM6
 * pad, PHYS drone, GRAIN cloud, 4-note chords on every step, DRUM) on the conductor's timeline, as a share of one core.
 * White-box (one copy compiled in) for the song; the rest through fel_core_t. The bound is loose (machines differ): it
 * catches an order of magnitude, the number is printed. */
#include "board.c"
#include "drive.h"
#include "check.h"
#include <time.h>

int main(void)
{
    const fel_core_t *c = fel_core_0();
    struct timespec a, b;
    double s;
    c->boot();
    web_test_heavy();
    c->play(drive_timeline);
    drive1(c, frames_ms(500), 0);
    clock_gettime(CLOCK_MONOTONIC, &a);
    drive1(c, 10 * FEL_RATE, 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    s = (double)(b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("  heavy song: 10 s of audio in %.3f s = %.1f %% of one core\n", s, s * 10);
    CHECK(song.playing && s < 1.0, "under 10 %% of one core (%.1f %%)", s * 10);
    return check_done("CPU (core/tests/test_cpu.c)");
}
