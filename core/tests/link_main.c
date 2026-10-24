/* The 2-copy link test's program (core/tests/run.sh): two copies of the core in one binary, both booted. */
#include "fel_core.h"
const fel_core_t *fel_core_0(void), *fel_core_1(void);
int main(void)
{
    fel_core_0()->boot();
    fel_core_1()->boot();
    return fel_core_0() == fel_core_1();
}
