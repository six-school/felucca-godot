/* The pool's copies of the core (core/build.sh: N compiled at build time, D3): copy n's table, n < fel_pool_size().
 * Each copy is one instance for the life of the process (fel_core_t.boot is once per copy). */
#ifndef FEL_POOL_H
#define FEL_POOL_H
#include "fel_core.h"
#ifdef __cplusplus
extern "C" {
#endif

int fel_pool_size(void);
const fel_core_t *fel_pool_core(int n);   /* 0 past the last */

#ifdef __cplusplus
}
#endif

#endif
