#include "felucca_api.c"
#include <stdio.h>
#include <time.h>
int main(void) {
    struct timespec a, b; uint32_t k;
    web_nor_erase(); web_master(700); web_boot();
    for (k = 0; k < 1500 * 441 / 10; k += 128) web_render(128);
    web_test_heavy(); fel_play(1);
    for (k = 0; k < 500 * 441 / 10; k += 128) web_render(128);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (k = 0; k < 10 * 44100; k += 128) web_render(128);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("heavy song: 10 s of audio in %.3f s = %.1f%% of one core (playing %u)\n", s, s * 10, web_playing());
}
