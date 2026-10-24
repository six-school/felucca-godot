#include "fel_core.h"
#include <stdio.h>
#include <math.h>
const fel_core_t *fel_core_0(void), *fel_core_1(void);
static double rms_ms(const fel_core_t *c, int ms)
{ float l[128], r[128]; double e = 0; int n = ms * 441 / 10, k, i;
  for (k = 0; k < n; k += 128) { c->render(128, l, r); for (i = 0; i < 128; i++) e += l[i] * l[i]; } return sqrt(e / n); }
int main(void)
{
    const fel_core_t *a = fel_core_0(), *b = fel_core_1();
    a->boot(); b->boot(); rms_ms(a, 1500); rms_ms(b, 1500);
    int an = a->engine_find("ANALOG"); a->engine_set(0, an); b->engine_set(0, an); rms_ms(a, 200); rms_ms(b, 200);
    a->param_set(0, 107, 20);  b->param_set(0, 107, 127);           /* ANALOG CUT: dark in A, open in B */
    printf("CUT  A=%d  B=%d\n", a->param_get(0, 107), b->param_get(0, 107));
    a->midi(0x90, 48, 110);                                            /* a note in A only */
    printf("note in A only: rms A=%.3f  B=%.3f\n", rms_ms(a, 400), rms_ms(b, 400));
    b->midi(0x90, 48, 110);
    printf("same note in both: rms A=%.3f  B=%.3f (A darker)\n", rms_ms(a, 400), rms_ms(b, 400));
    return 0;
}
