/* Two instances, the same song, started in the same block, then 10 minutes: the same positions and the same audio? */
#include "fel_core.h"
#include <stdio.h>
#include <string.h>
const fel_core_t *fel_core_0(void), *fel_core_1(void);
void fel_heavy_0(void), fel_heavy_1(void);
void fel_pos_0(int, uint32_t *, uint32_t *), fel_pos_1(int, uint32_t *, uint32_t *);
int main(void)
{
    const fel_core_t *a = fel_core_0(), *b = fel_core_1();
    float al[128], ar[128], bl[128], br[128];
    uint64_t k, diff = 0, n = 600ull * 44100;
    uint32_t t, i0, p0, i1, p1, same = 1;
    a->boot(); b->boot();
    for (k = 0; k < 66150; k += 128) { a->render(128, al, ar); b->render(128, bl, br); }
    fel_heavy_0(); fel_heavy_1();                              /* one game-side command, applied to both */
    for (k = 0; k < n; k += 128) {
        a->render(128, al, ar); b->render(128, bl, br);
        if (memcmp(al, bl, sizeof al) || memcmp(ar, br, sizeof ar)) diff++;
    }
    for (t = 0; t < 4; t++) { fel_pos_0(t, &i0, &p0); fel_pos_1(t, &i1, &p1); same &= i0 == i1 && p0 == p1;
        printf("track %u: A step %2u + %5u samples, B step %2u + %5u samples\n", t + 1, i0, p0, i1, p1); }
    printf("10 min of the heavy song: %s positions, %llu of %llu audio blocks differ\n", same ? "identical" : "DIFFERENT",
           (unsigned long long)diff, (unsigned long long)(n / 128));
    return 0;
}
